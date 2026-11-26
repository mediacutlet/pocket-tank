/* render_big.c - THE BIG SCREENS' PAGES (0.4.3, Strato 11-16: "making better use of the screen real estate and
 * leaning into side by side ipad style layouts like the original P4 keeper page"; his mockups approved the same day).
 *
 * October's home line had these (render_wide.c, kept in the tree as it was): a RAIL of the pages down the left, a LIST
 * pane and a DETAIL pane that is always open for the selected row. This file is that frame brought to 0.4.2, a page
 * at a time, INCLUDED at the end of render.c under TANK_P4 (it shares the renderer's primitives and the pages' state).
 * A page built here takes the small page's public names; the small page stays whole under <name>_small (render.c
 * renames it for a big screen before its definitions), so a state this file does not draw yet can still be handed to it.
 *
 * Built so far: the KEEPER page - ten fish in the list, a fish's pane with three tabs: MILESTONES, STORY, TRAITS
 * (what MORE's three pages show on a pocket tank). Everything is drawn in FRAME coordinates. */
#include <ctype.h>

/* THE BACKGROUNDS' PICTURES on the big screens (0.4.3, THE WIDE FRAME): the wide scenes themselves, at the two sizes the
   shop draws them - its pane's and its rows' (tools/bg_thumbs_big.py makes them from the 10.1's renders) */
#include "bg_thumbs_big.inc"
static void big_bg_thumb(ctx_t *c, int x, int y, int bg, bool big) {
    if ((unsigned)bg >= BG_N) return;
    if (big) { const uint8_t *p = BGT[bg].big; const uint16_t *pal = BGT[bg].pal;
        for (int j = 0; j < BGT_BIG_H; j++) for (int i = 0; i < BGT_BIG_W; i++) px565_blend(c, x + i, y + j, pal[*p++], 255); }
    else { const uint16_t *p = BGT[bg].small;
        for (int j = 0; j < BGT_SMALL_H; j++) for (int i = 0; i < BGT_SMALL_W; i++) px565_blend(c, x + i, y + j, *p++, 255); }
}
#include "setup.h"
static int g_w_tab;                   /* a fish's pane: 0 MILESTONES, 1 STORY, 2 TRAITS */
static short g_w_bowl_top = -1;       /* THE WAITING ROW's top, where the page last drew it */
/* ---- the frame ---- */
#define WR_X       32                 /* the rail */
#define WR_Y0      32
#define WR_W       200
#define WR_H       64
#define WR_DY      72
#define WR_CLOSE_Y (TANK_H - 96)       /* 704 on the 10.1, 624 on the Tab5 */
#define WR_N       5                  /* KEEPER, UPGRADES, MY TANKS, VISITORS, SETTINGS */
#define WC_X       264                /* the content area */
#define WC_Y       32
#define WC_W       984
#define WC_H       (TANK_H - 64)       /* 736 on the 10.1, 656 on the Tab5 */
/* KEEPER: the list pane and the detail pane */
#define WL_X       WC_X
#define WL_W       480
#define WL_ROW_Y0  (WC_Y + 16)
/* THE TAB5 (2026-10-19): the same frame 80 px shorter - the rows that stack down the glass are closer (they are
 * physically small there anyway: ~294 ppi against the 10.1's ~149); everything across is as on the 10.1 */
#ifdef TANK_TAB5
#define WL_ROW_H   50                 /* 12 rows in 600 + the TANK row's gap, of 656 px */
#else
#define WL_ROW_H   56                 /* 12 rows (10 swimmers + NEW FRY + TANK) in 672 of 736 px */
#endif
#define WL_FISH_X  (WL_X + 48)        /* a row's portrait centre */
#define WL_NAME_X  (WL_X + 96)
#define WL_BADGE_X0 (WL_X + 230)      /* six badges at a 40 px pitch end at +462; NEW FRY at scale 3 ends at +219 */
#define WL_BADGE_DX 40
#define WD_X       776
#define WD_W       472
#define WD_BTN_W   180
#define WD_BTN_H   48
#define WD_BTN_GAP 20
#define WD_DIV_GAP 20                 /* clear water on each side of a divider line (Strato 2026-10-13: the one under RENAME / SELL sat ON the buttons;
                                         "more padding around all these divider lines") */
#define WD_BTN_Y   (WC_Y + 250)       /* a fish's RENAME / SELL */
#define WD_TAB_Y   (WD_BTN_Y + WD_BTN_H + 14)   /* a fish's three tabs, under RENAME / SELL */
#define WD_TAB_H   40
#define WD_TAB_N   3
static const char *const WD_TAB_WORDS[WD_TAB_N] = { "MILESTONES", "STORY", "TRAITS" };
static const short WD_TAB_X[WD_TAB_N] = { 24, 176, 312 }, WD_TAB_W[WD_TAB_N] = { 144, 128, 136 };
#define WD_LIST_Y  (WD_TAB_Y + WD_TAB_H + 16)   /* what the tab shows: a fish's badges listed, its story, its traits */
#ifdef TANK_TAB5                      /* (the pane ends at y 688 there) */
#define WD_LIST_DY 48                 /* six badges from y 400 end at 682 */
#define WD_STORY_DY 30
#define WD_FORM_DY 24
#define WD_NAT_GAP 14
#define WD_NAT_DY  28
#else
#define WD_LIST_DY 60
#define WD_STORY_DY 36
#define WD_FORM_DY 28
#define WD_NAT_GAP 36
#define WD_NAT_DY  34
#endif
#define WD_GRID_X0 (WD_X + 40)        /* the tank's badges, a grid */
#define WD_GRID_Y  (WC_Y + 226 + WD_DIV_GAP + 8)   /* ... under its divider (a picked badge's box stands 8 px proud) */
#define WD_GATE_DY 56                 /* the fry pane's gates */
#define WL_TANK_GAP 24                /* the list: the TANK row stands apart from the fish above it, the divider in the middle */
#define WD_GRID_DX 64
#define WD_GRID_DY 56
#define WD_GRID_COLS 6
/* the rail's words, in order */
enum { WR_KEEPER = 0, WR_UPGRADES, WR_TANKS, WR_VISITORS, WR_SETTINGS };
static const char *const WR_WORDS[WR_N] = { "KEEPER", "UPGRADES", "MY TANKS", "VISITORS", "SETTINGS" };
#define W_BTN_FILL 0x1c2f36
#define W_PANE_BG 0x071a21

/* ---- the KEEPER page's state ---- */
enum { WS_NONE = 0, WS_FISH, WS_FRY, WS_TANK, WS_BOWL };   /* (WS_BOWL: THE WAITING ROW - who is in the travel bowl; never selected, a tap brings its card) */
static int  g_w_kind;                 /* what the detail pane shows */
static int  g_w_slot = -1;            /* WS_FISH: the fish's slot (a visitor's too) */
static int  g_w_tank_k = -1;          /* WS_TANK: the badge whose words show under the grid, or -1 */
static int  g_w_gate = -1;            /* WS_FRY: the gate whose HOW shows, or -1 = the first still owed */

/* the rows of the list, in order: home fish here, visitors, NEW FRY (while
   the tank can grow), TANK */
typedef struct { int kind, slot; } wrow_t;
static int w_rows(const tank_t *t, wrow_t *out, int max, int nreq) {
    int n = 0;
    for (int i = 0; i < N_FISH_SLOTS && n < max; i++)
        if (tank_swims(t, i)) { out[n].kind = WS_FISH; out[n].slot = i; n++; }
    if (nreq > 0 && n < max) { out[n].kind = WS_FRY; out[n].slot = -1; n++; }
    if (progression_bowl_has() && n < max - 1) { out[n].kind = WS_BOWL; out[n].slot = -1; n++; }
    if (n < max) { out[n].kind = WS_TANK; out[n].slot = -1; n++; }
    return n;
}
#define W_ROWS_MAX (N_FISH_SLOTS + 3)
static int w_row_of(const wrow_t *rows, int n, int kind, int slot) {
    for (int i = 0; i < n; i++) if (rows[i].kind == kind && (kind != WS_FISH || rows[i].slot == slot)) return i;
    return -1;
}
/* a row's top: the TANK row (always the last) stands WL_TANK_GAP lower */
static int w_row_top(const wrow_t *rows, int r) { return WL_ROW_Y0 + r * WL_ROW_H + (rows[r].kind == WS_TANK ? WL_TANK_GAP : 0); }
/* the row under a finger at frame y, or -1: each row's band is its own height (4 px of slop above the first) */
static int w_row_at(const wrow_t *rows, int n, float y) {
    for (int r = 0; r < n; r++) { int top = w_row_top(rows, r); if (y >= top - (r == 0 ? 4 : 0) && y < top + WL_ROW_H) return r; }
    return -1;
}
static void w_select(int kind, int slot) {
    if (kind != g_w_kind || slot != g_w_slot) { g_ms_sell_armed = false; g_ms_sell_asked = false; }
    g_w_kind = kind; g_w_slot = slot;
}
/* the selection must point at something: the first row when nothing is */
static void w_settle(const tank_t *t, const wrow_t *rows, int n) {
    if (g_w_kind == WS_FISH && (g_w_slot < 0 || !tank_swims(t, g_w_slot))) g_w_kind = WS_NONE;
    if (g_w_kind == WS_FRY && w_row_of(rows, n, WS_FRY, -1) < 0) g_w_kind = WS_NONE;
    if (g_w_kind == WS_NONE && n > 0) { g_w_kind = rows[0].kind; g_w_slot = rows[0].slot; }
}

/* ---- pieces ---- */
static void w_rail(ctx_t *c, int lit, bool visitors) {
    for (int i = 0; i < WR_N; i++) {
        int y = WR_Y0 + i * WR_DY;
        bool dim = i == WR_VISITORS && !visitors;
        if (i == lit) { button(c, WR_X, y, WR_W, WR_H, MSP_TEAL, MSP_TEAL, "", 3);
                        draw_text(c, WR_X + (WR_W - text_w(WR_WORDS[i], 3)) / 2, y + (WR_H - 21) / 2, 3, MSP_INK, WR_WORDS[i]); }
        else button(c, WR_X, y, WR_W, WR_H, W_BTN_FILL, dim ? MSP_DIM : MSP_TEAL, WR_WORDS[i], 3);
    }
    button(c, WR_X, WR_CLOSE_Y, WR_W, WR_H, W_BTN_FILL, MSP_TEAL, "CLOSE", 3);
}
/* a tap on the rail: the button's index, WR_N = CLOSE, -1 = not the rail
   (generous: the whole band from the glass to the content's edge) */
static int w_rail_hit(float x, float y) {
    if (x >= WC_X - 8) return -1;
    if (y >= WR_CLOSE_Y - 12) return WR_N;
    for (int i = 0; i < WR_N; i++) { int y0 = WR_Y0 + i * WR_DY; if (y >= y0 - 6 && y < y0 + WR_DY - 2) return i; }
    return -1;
}
static void w_pane(ctx_t *c, int x, int w, uint32_t edge) {
    rect_fill(c, x, WC_Y, w, WC_H, W_PANE_BG);
    rect_edge(c, x, WC_Y, w, WC_H, edge);
}
/* a fry gate's art in a 32 px cell at frame (x, y): the icon, or the youngest fish (GROW) */
static void w_fry_badge(ctx_t *c, uint16_t *fb, int stride, const tank_t *t, int x, int y, const fry_req_t *r) {
    const icon_t *ic = fry_req_icon(r->kind);
    if (ic) {
        int off = (MSP_ICON - ic->w) / 2;
        if (r->met) blit_icon(c, x + off, y + off, ic, 255); else blit_icon_locked(c, x + off, y + off, ic);
    } else if (t->n_fish > 0) {
        const fish_t *f = &t->fish[t->n_fish - 1];
        float sz = f->size > 0.7f ? 0.7f : f->size;
        uint32_t b = r->met ? f->color : MSP_DIM, fn = r->met ? f->fin : MSP_DIM, a = r->met ? f->accent : MSP_DIM;
        render_fish_preview(fb, stride, x + MSP_ICON / 2 - PAGE_X, y + MSP_ICON / 2 - PAGE_Y, sz, b, fn, a, f, FISH_ROOM_ROW, t->clock);
    }
}
static const char *w_upper_word(const char *s, char *buf, size_t n) {   /* the tables' words are lower case; the font is one case */
    size_t i = 0; for (; s[i] && i + 1 < n; i++) buf[i] = (char)toupper((unsigned char)s[i]); buf[i] = 0; return buf;
}

/* THE PARAGON (0.4.3) on a fish's pane, as the pocket card has it (render.c, the fish card): PARAGON and its part once
   it has evolved; SOMETHING IS HAPPENING through its last hour before; else the stage's word. While its three wait,
   a big EVOLVE in the pane's top right, the portrait moved left for it (Strato 11-24: "bigger and more obvious. its an
   important moment"). A visitor's pane keeps its own lines. */
static bool w_evo_ready(const tank_t *t, int slot) { return slot >= 0 && slot < t->n_fish && tank_evo_ready(&t->fish[slot]) && !t->fish[slot].away; }
static const char *w_stage_line(const tank_t *t, int slot, char *buf, size_t n) {
    const fish_t *f = &t->fish[slot];
    if (slot < t->n_fish && f->evo) { snprintf(buf, n, "PARAGON - %s", tank_evo_name(f->evo)[0] ? tank_evo_name(f->evo) : "?"); return buf; }
    if (slot < t->n_fish && progression_evo_sparkle(t, slot)) return "SOMETHING IS HAPPENING";
    return STAGE_WORDS[f->stage & 3];
}
/* a home fish's word in the list: its stage, or PARAGON once it has evolved (Strato 11-24: an evolved fish read ELDER
   there; the pocket list has no word, its portrait wears the part) */
static const char *w_row_word(const fish_t *f) { return f->evo ? "PARAGON" : STAGE_WORDS[f->stage & 3]; }
static int w_stage_sc(const char *s) { return text_w(s, 3) <= WD_W - 48 ? 3 : 2; }   /* (a long part's name at the caption size) */
#define WD_STAGE_Y (WC_Y + 172)
#define WD_EVO_X   (WD_X + 280)        /* EVOLVE's plate: right of the portrait, level with it */
#define WD_EVO_Y   (WC_Y + 46)
#define WD_EVO_W   (WD_W - 280 - 24)
#define WD_EVO_H   64                 /* (RENAME / SELL are 48) */
#define WD_EVO_SLOP 12                /* its tap reaches this far past the plate */
#define WD_EVO_FISH_X (WD_X + 144)    /* the portrait while EVOLVE is up */
#define WD_EVO_BOX_X0 (WD_X + 272)    /* EVOLVE's moving box (its stars twinkle): the plate and its stars */
#define WD_EVO_BOX_Y0 (WC_Y + 12)
#define WD_EVO_BOX_X1 (WD_X + WD_W - 6)
#define WD_EVO_BOX_Y1 (WC_Y + 132)
static void w_star(ctx_t *c, int x, int y, int r, int al) {     /* a four-point star, as the sparkle hour's */
    for (int k = -r; k <= r; k++) { const int a = al * (r + 3 - abs(k)) / (r + 3); px_blend(c, x + k, y, 0xfff4c8, a); if (k) px_blend(c, x, y + k, 0xfff4c8, a); }
    for (int k = -1; k <= 1; k++) for (int j = -1; j <= 1; j++) px_blend(c, x + k, y + j, 0xffffff, al);
}
static void w_evolve_draw(ctx_t *c, double now) {
    const int x = WD_EVO_X, y = WD_EVO_Y, w = WD_EVO_W, h = WD_EVO_H, tw = text_w("EVOLVE", 4);
    g_bb_on = false;                                           /* (WK_BOX measures a fish's ink, in page px: this is fixed, inside its box) */
    /* EVOLVE (Strato's pick, 11-24): a solid gold plate, dark words, a dark line inside its edge - a plate, big type and
       a frame, not the gold alone - and stars twinkling slowly round it, the sparkle hour's four-point stars */
    rect_fill(c, x, y, w, h, 0xffd166);
    rect_edge(c, x, y, w, h, 0xfff4c8); rect_edge(c, x + 4, y + 4, w - 8, h - 8, 0x3a2c08);
    draw_text(c, x + (w - tw) / 2, y + (h - 28) / 2, 4, MSP_INK, "EVOLVE");
    static const short PX[6] = { 8, 64, 132, 174, 150, 30 }, PY[6] = { -16, -22, -18, 22, 76, 74 };   /* round the plate, from its top left (inside its box: no ink is measured there - the kept-equals-whole test watches it) */
    for (int i = 0; i < 6; i++) {
        const float ph = (float)fmod(now * 0.45 + i * 0.29, 1.0), a = ph < 0.5f ? ph * 2 : (1 - ph) * 2;
        w_star(c, x + PX[i], y + PY[i], i % 3 == 0 ? 9 : 6, (int)(255 * fminf(1, 0.25f + a * 1.2f)));
    }
}
/* ---- the KEEPER page ---- */
/* THE WIDE PAGE KEPT. Drawn whole the page is 40 ms on a P4 (a megapixel of ink, two panes, a dozen rows of type), and
 * nothing in it moves but its fish. So a buffer that holds the page as it is (the same key: every word and badge on
 * it) has only its MOVING BOXES drawn again - each a fixed cell given back to its ground, then its fish - and the port
 * turns those tiles alone (render_milestones_changed). A fish that swims out of its cell, a draw from other hands
 * (g_ctx_other), a frame without the page: the next frame is whole. The whole frame draws the boxes by the same
 * lines, so a kept frame is the whole one pixel for pixel. */
#define WK_MAX (W_ROWS_MAX + FRY_REQ_MAX + 2)   /* (+ the pane's fish, EVOLVE's stars) */
typedef struct { const uint16_t *fb; uint32_t key; unsigned frame, other; bool ok; } wk_t;
static wk_t g_wk[2]; static unsigned g_wk_frame, g_wk_run0;
static short g_wk_box[WK_MAX * 4]; static int g_wk_n = -1;      /* this frame's moving boxes (frame px, inclusive), or -1: drawn whole */
static unsigned g_wk_kept_n, g_wk_whole_n;
void render_big_keeper_counts(unsigned *kept, unsigned *whole) { *kept = g_wk_kept_n; *whole = g_wk_whole_n; }
#define WK_MIX(h, v) (((h) ^ (uint32_t)(v)) * 16777619u)
static uint32_t wk_key(const tank_t *t, const wrow_t *rows, int n, const fry_req_t *req, int nreq, bool staged) {
    uint32_t h = 2166136261u;
    h = WK_MIX(h, 0x4b);                                             /* (the page: the UPGRADES page keeps the same two records) */
    h = WK_MIX(h, g_w_kind); h = WK_MIX(h, g_w_slot); h = WK_MIX(h, g_w_tab); h = WK_MIX(h, g_w_gate); h = WK_MIX(h, g_w_tank_k);
    h = WK_MIX(h, g_ms_sell_armed); h = WK_MIX(h, g_ms_sell_asked); h = WK_MIX(h, n); h = WK_MIX(h, staged); h = WK_MIX(h, collection_count() > 0);
    h = WK_MIX(h, (int)t->sd_balance); h = WK_MIX(h, t->tank_ms_bits); h = WK_MIX(h, t->tank_ms_seen); h = WK_MIX(h, t->n_fish);
    for (int r = 0; r < n; r++) {
        h = WK_MIX(h, rows[r].kind); h = WK_MIX(h, rows[r].slot);
        if (rows[r].kind != WS_FISH) continue;
        const fish_t *f = &t->fish[rows[r].slot];
        for (const char *q = f->name; *q; q++) h = WK_MIX(h, *q);
        h = WK_MIX(h, f->stage); h = WK_MIX(h, f->evo); h = WK_MIX(h, f->ms_bits); h = WK_MIX(h, f->ms_seen); h = WK_MIX(h, f->color); h = WK_MIX(h, f->accent); h = WK_MIX(h, f->fin);
        if (tank_is_visitor(rows[r].slot)) h = WK_MIX(h, t->vis_badge[rows[r].slot - N_FISH_MAX]);
    }
    for (int k = 0; k < nreq; k++) { h = WK_MIX(h, req[k].kind); h = WK_MIX(h, req[k].met); h = WK_MIX(h, (int)(req[k].frac * 140)); for (const char *q = req[k].progress; *q; q++) h = WK_MIX(h, *q); }
    if (g_w_kind == WS_FISH && g_w_slot >= 0 && g_w_slot < N_FISH_SLOTS) {       /* the pane's own words */
        const fish_t *f = &t->fish[g_w_slot];
        h = WK_MIX(h, f->shape); h = WK_MIX(h, f->fins); h = WK_MIX(h, f->pattern);
        if (g_w_slot < t->n_fish) { h = WK_MIX(h, progression_fish_sellable(t, g_w_slot)); h = WK_MIX(h, progression_fish_value(t, g_w_slot)); }
        if (g_w_slot < t->n_fish) { h = WK_MIX(h, give_look(t, g_w_slot)); h = WK_MIX(h, move_shown()); }
        if (g_w_slot < t->n_fish) { h = WK_MIX(h, f->evo); h = WK_MIX(h, tank_evo_ready(f) && !f->away); h = WK_MIX(h, progression_evo_sparkle(t, g_w_slot)); }   /* (the paragon's stage line) */
        if (g_w_tab) { h = WK_MIX(h, f->eaten); h = WK_MIX(h, f->eaten_player); h = WK_MIX(h, (int)(f->trust / 2 + 0.0001f)); h = WK_MIX(h, (int)(f->size * 20));
                       h = WK_MIX(h, (int)(f->bold * 240)); h = WK_MIX(h, (int)(f->sociable * 240)); h = WK_MIX(h, (int)(t->clock / 60)); }
    }
    return h;
}
/* every moving box: its cell back to its ground, then its fish; false = one swam out of its cell */
static bool w_anim_all(const tank_t *t, uint16_t *fb, int stride, const wrow_t *rows, int n, const fry_req_t *req, int nreq, bool staged) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    bool in = true; g_wk_n = 0;
#define WK_BOX(x0_, y0_, x1_, y1_, bg_, draw_) do { const int bx0 = (x0_), by0 = (y0_), bx1 = (x1_), by1 = (y1_); \
        rect_fill(&c, bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1, (bg_)); g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -(1 << 20); draw_; g_bb_on = false; \
        struct { int x0, y0, x1, y1; } b_ = { g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1 }; \
        if (b_.x0 <= b_.x1 && (b_.x0 + PAGE_X < bx0 || b_.x1 + PAGE_X > bx1 || b_.y0 + PAGE_Y < by0 || b_.y1 + PAGE_Y > by1)) in = false; \
        if (g_wk_n < WK_MAX) { short *q = g_wk_box + g_wk_n * 4; q[0] = (short)bx0; q[1] = (short)by0; q[2] = (short)bx1; q[3] = (short)by1; g_wk_n++; } } while (0)
    for (int r = 0; r < n; r++) {
        const int top = w_row_top(rows, r);
        const bool sel = rows[r].kind == g_w_kind && (g_w_kind != WS_FISH || rows[r].slot == g_w_slot);
        const uint32_t bg = sel ? W_BTN_FILL : W_PANE_BG;
        if (rows[r].kind == WS_FISH) {
            const fish_t *f = &t->fish[rows[r].slot];
            WK_BOX(WL_X + 10, top + 1, WL_X + 93, top + WL_ROW_H - 4, bg,
                   fish_as_is(fb, stride, WL_FISH_X - PAGE_X, top + WL_ROW_H / 2 - PAGE_Y, f->size, f, f->color, f->fin, f->accent, FISH_ROOM_ROW, true, t->clock));
        } else if (rows[r].kind == WS_FRY) {
            const uint32_t fry_rgb = staged ? MSP_TEAL : MSP_DIM;
            WK_BOX(WL_X + 10, top + 1, WL_X + 93, top + WL_ROW_H - 4, bg,
                   render_fish_preview(fb, stride, WL_FISH_X - PAGE_X, top + WL_ROW_H / 2 - PAGE_Y, 0.55f, fry_rgb, fry_rgb, fry_rgb, NULL, FISH_ROOM_ROW, t->clock));
            for (int k = 0; k < nreq; k++) if (!fry_req_icon(req[k].kind)) {       /* GROW: the youngest fish, in the gate's cell */
                const int x = WL_BADGE_X0 + k * WL_BADGE_DX, y = top + (WL_ROW_H - MSP_ICON) / 2 - 2;
                WK_BOX(x - 3, y - 1, x + MSP_ICON + 2, y + MSP_ICON, bg, w_fry_badge(&c, fb, stride, t, x, y, &req[k]));
            }
        }
    }
    if (g_w_kind == WS_FISH && g_w_slot >= 0) {
        const fish_t *f = &t->fish[g_w_slot]; const bool evo = w_evo_ready(t, g_w_slot);
        const int CX = evo ? WD_EVO_FISH_X : WD_X + WD_W / 2;     /* (EVOLVE up: the portrait to the left, the plate right) */
        WK_BOX(evo ? WD_X + 16 : WD_X + 40, WC_Y + 8, evo ? WD_EVO_BOX_X0 - 1 : WD_X + WD_W - 40, WC_Y + 132, W_PANE_BG,
               fish_as_is(fb, stride, CX - PAGE_X, WC_Y + 78 - PAGE_Y, f->size * 1.6f, f, f->color, f->fin, f->accent, FISH_ROOM_CARD, false, t->clock));
        if (evo) WK_BOX(WD_EVO_BOX_X0, WD_EVO_BOX_Y0, WD_EVO_BOX_X1, WD_EVO_BOX_Y1, W_PANE_BG, w_evolve_draw(&c, t->clock));
    }
    return in;
}
/* what this page does not draw yet is the small page's, whole: the GIVE flow and the travel bowl's card (one modal,
   g_gm), a gift offered */
static bool w_small_has_it(const tank_t *t) { return g_gm.on || render_gift_up(t); }
/* a fish's buttons, left to right: the small card's (ms_card_btns) without MORE - the tabs are its pages here.
   RENAME | GIVE (linked) | MOVE (a second tank, or a fish in the bowl) | SELL, sharing the pane's width */
static int w_btns(const tank_t *t, int fish, int id[5]) {
    int all[5], n = ms_card_btns(t, fish, all), m = 0;
    for (int i = 0; i < n; i++) if (all[i] != CB_MORE) id[m++] = all[i];
    return m;
}
#define WD_BTNS_X  (WD_X + 24)
#define WD_BTNS_W  (WD_W - 48)
#define WD_BTNS_GAP 12
static int w_btn_w(int n) { return (WD_BTNS_W - (n - 1) * WD_BTNS_GAP) / n; }
static int w_btn_x(int n, int i) { return WD_BTNS_X + i * (w_btn_w(n) + WD_BTNS_GAP); }
void render_milestones(const tank_t *t, uint16_t *fb, int stride) {
    if (w_small_has_it(t)) { g_wk[0].ok = g_wk[1].ok = false; g_wk_n = -1; render_milestones_small(t, fb, stride); return; }
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    wrow_t rows[W_ROWS_MAX]; int n = w_rows(t, rows, W_ROWS_MAX, nreq);
    w_settle(t, rows, n);
    const uint32_t key = wk_key(t, rows, n, req, nreq, staged);
    if (!g_ms_whole_dbg) {
        if (g_wk_frame != g_rframe && g_wk_frame + 1 != g_rframe) g_wk_run0 = g_rframe;   /* a frame without the page: a new run */
        g_wk_frame = g_rframe;
    }
    wk_t *kp = g_wk[0].fb == fb ? &g_wk[0] : g_wk[1].fb == fb ? &g_wk[1] : g_wk[0].frame <= g_wk[1].frame ? &g_wk[0] : &g_wk[1];
    const bool keep = !g_ms_whole_dbg && kp->fb == fb && kp->ok && kp->key == key && kp->other == g_ctx_other && stride == TANK_W
                      && kp->frame < g_rframe && kp->frame >= g_wk_run0 && g_w_kind == WS_FISH;
    g_ms_in = true; g_dirty_hold = true; g_wk_n = -1;
    if (keep && w_anim_all(t, fb, stride, rows, n, req, nreq, staged)) { kp->frame = g_rframe; g_wk_kept_n++; g_ms_in = false; return; }
    g_wk_whole_n++; g_wk_n = -1;
    ctx_t c = ctx_full(fb, stride, 1.0f);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    w_rail(&c, WR_KEEPER, collection_count() > 0);
    /* the list */
    w_pane(&c, WL_X, WL_W, 0x2c4a52);
    for (int r = 0; r < n; r++) {
        int top = w_row_top(rows, r);
        bool sel = rows[r].kind == g_w_kind && (g_w_kind != WS_FISH || rows[r].slot == g_w_slot);
        if (sel) { rect_fill(&c, WL_X + 8, top, WL_W - 16, WL_ROW_H - 2, W_BTN_FILL); rect_edge(&c, WL_X + 8, top, WL_W - 16, WL_ROW_H - 2, MSP_TEAL); }
        if (rows[r].kind == WS_FISH) {
            const fish_t *f = &t->fish[rows[r].slot];
            bool vis = tank_is_visitor(rows[r].slot);
            draw_text(&c, WL_NAME_X, top + 6, 3, 0xffffff, f->name);
            if (vis) {
                int k = rows[r].slot - N_FISH_MAX;
                draw_text(&c, WL_NAME_X, top + 32, 2, 0xe8c872, "VISITING");
                if (t->vis_badge[k] >= 0) link_badge_draw(&c, WL_BADGE_X0 + 16, top + WL_ROW_H / 2, 12, t->vis_badge[k], 255);
            } else {
                draw_text(&c, WL_NAME_X, top + 32, 2, 0x5f8a92, w_row_word(f));
                /* (the growth strip is in the pane, under the stage's word: here it ran under the first badge - Strato on the glass, 11-16) */
                for (int k = 0; k < 6; k++) {
                    uint32_t bit = FISH_BADGES[k].bit;
                    badge(&c, WL_BADGE_X0 + k * WL_BADGE_DX, top + (WL_ROW_H - MSP_ICON) / 2, FISH_BADGES[k].icon, (f->ms_bits & bit) != 0, (f->ms_seen & bit) == 0);
                }
            }
        } else if (rows[r].kind == WS_FRY) {
            draw_text(&c, WL_NAME_X, top + 6, 3, MSP_TEAL, "NEW FRY");
            for (int k = 0; k < nreq; k++) rect_fill(&c, WL_NAME_X + k * 16, top + 36, 10, 6, req[k].met ? MSP_TEAL : MSP_DIM);
            for (int k = 0; k < nreq; k++) {
                int x = WL_BADGE_X0 + k * WL_BADGE_DX, y = top + (WL_ROW_H - MSP_ICON) / 2 - 2;
                if (fry_req_icon(req[k].kind)) w_fry_badge(&c, fb, stride, t, x, y, &req[k]);   /* (GROW's fish moves: w_anim_all) */
                if (!req[k].met) {
                    int w = (int)(MSP_ICON * req[k].frac + 0.5f);
                    rect_fill(&c, x, y + MSP_ICON + 4, MSP_ICON, 3, MSP_DIM);
                    if (w > 0) rect_fill(&c, x, y + MSP_ICON + 4, w, 3, MSP_TEAL);
                }
            }
        } else if (rows[r].kind == WS_BOWL) {                         /* THE WAITING ROW (0.4.2) */
            char l[32]; snprintf(l, sizeof l, "%s WAITS IN THE BOWL", progression_bowl_name());
            g_w_bowl_top = (short)top;
            bowl_icon(&c, WL_FISH_X - BOWL_ICON / 2, top + (WL_ROW_H - BOWL_ICON) / 2, true);
            draw_text(&c, WL_NAME_X, top + 8, 2, 0xffffff, l);
            draw_text(&c, WL_NAME_X, top + 30, 2, MSP_TEAL, "TAP HERE TO LET IT IN");
        } else {                                                     /* TANK */
            for (int x = WL_X + 24; x < WL_X + WL_W - 24; x++) px_blend(&c, x, top - WL_TANK_GAP / 2 - 1, MSP_DIM, 200);
            blit_icon(&c, WL_FISH_X - MSP_ICON / 2, top + 4, &icon_ms_sand_dollar, 255);
            char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
            draw_text(&c, WL_FISH_X - text_w(bal, 2) / 2, top + 40, 2, 0xffffff, bal);
            draw_text(&c, WL_NAME_X, top + 6, 3, MSP_TEAL, "TANK");
            for (int k = 0; k < POP_CAP; k++) fish_glyph(&c, WL_NAME_X + 8 + k * 14, top + 40, 2.8f, k < t->n_fish ? MSP_TEAL : MSP_DIM);
            int nb = tank_badge_n(t);
            for (int k = 0; k < 6 && k < nb; k++) {                     /* the first six here; every one in the pane */
                const badge_t *tb = tank_badge(t, k);
                g_ms_tshown |= tb->bit;                               /* (seen at the page's close, as the small page's) */
                badge(&c, WL_BADGE_X0 + k * WL_BADGE_DX, top + (WL_ROW_H - MSP_ICON) / 2, tb->icon, (t->tank_ms_bits & tb->bit) != 0, (t->tank_ms_seen & tb->bit) == 0);
            }
        }
    }
    /* the detail pane */
    w_pane(&c, WD_X, WD_W, MSP_TEAL);
    const int CX = WD_X + WD_W / 2;
    char up[64];
    if (g_w_kind == WS_FISH && g_w_slot >= 0) {
        const fish_t *f = &t->fish[g_w_slot];
        bool vis = tank_is_visitor(g_w_slot);
        draw_text(&c, CX - text_w(f->name, 4) / 2, WC_Y + 136, 4, 0xffffff, f->name);
        if (vis) {
            int k = g_w_slot - N_FISH_MAX;
            char from[40]; snprintf(from, sizeof from, "VISITING FROM THE %s TANK", t->vis_badge[k] >= 0 ? BADGE_WORDS[t->vis_badge[k] % LINK_BADGES] : "FAR");
            int fx = CX - (text_w(from, 2) + (t->vis_badge[k] >= 0 ? 30 : 0)) / 2;
            if (t->vis_badge[k] >= 0) { link_badge_draw(&c, fx + 10, WC_Y + 184, 9, t->vis_badge[k], 255); fx += 30; }
            draw_text(&c, fx, WC_Y + 176, 2, 0xe8c872, from);
            draw_text(&c, CX - text_w(STAGE_WORDS[f->stage & 3], 3) / 2, WC_Y + 204, 3, MSP_TEAL, STAGE_WORDS[f->stage & 3]);
            char form[40]; snprintf(form, sizeof form, "%s  %s  %s", SHAPE_NAMES[f->shape % SHAPE_N], FINS_NAMES[f->fins % FINS_N], PATTERN_NAMES[f->pattern % PATTERN_N]);
            draw_text(&c, CX - text_w(form, 2) / 2, WC_Y + 236, 2, 0x5f8a92, form);
            draw_text(&c, CX - text_w("TAP IT IN THE TANK FOR ITS PASSPORT", 2) / 2, WC_Y + 280, 2, MSP_TEAL, "TAP IT IN THE TANK FOR ITS PASSPORT");
        } else {
            {   /* its stage line: the stage's word, or the paragon's (w_stage_line) - plain ELDER while EVOLVE is up (its plate:
                   w_anim_all, a moving box - its stars twinkle) */
                char evo[40]; const char *line = w_stage_line(t, g_w_slot, evo, sizeof evo);
                const int sc = w_stage_sc(line);
                draw_text(&c, CX - text_w(line, sc) / 2, WD_STAGE_Y + (sc == 2 ? 4 : 0), sc, MSP_TEAL, line);
            }
            {   /* its growth strip: fry -> elder, lit up to the stage reached (its form's words are the TRAITS tab's) */
                static const int GX[4] = { 0, 24, 54, 90 };
                for (int g = 0; g < 4; g++) fish_glyph(&c, CX - 58 + GX[g] + 10, WC_Y + 208, 3.3f + g * 1.35f, g <= (int)f->stage ? f->accent : MSP_DIM);
            }
            /* what it would fetch, or why it stays (the small card's line) */
            bool can = progression_fish_sellable(t, g_w_slot);
            int worth = progression_fish_value(t, g_w_slot);
            if (!can) g_ms_sell_armed = false; else g_ms_sell_asked = false;
            char sub[40] = "";
            if (g_ms_sell_armed) snprintf(sub, sizeof sub, "TAP AGAIN TO SELL");
            else if (can) snprintf(sub, sizeof sub, "WORTH %d SAND DOLLARS", worth);
            else if (g_ms_sell_asked) snprintf(sub, sizeof sub, t->n_fish <= FISH_KEEP_MIN ? "KEEP AT LEAST TWO FISH" : "NAME THE NEW FRY FIRST");
            if (sub[0]) draw_text(&c, CX - text_w(sub, 2) / 2, WC_Y + 226, 2, 0xffffff, sub);
            const int by = WD_BTN_Y; int id[5]; const int nb = w_btns(t, g_w_slot, id), bw = w_btn_w(nb), sc = nb > 3 ? 2 : 3;
            const int look = give_look(t, g_w_slot);
            for (int b = 0; b < nb; b++) {
                const int bx = w_btn_x(nb, b);
                if (id[b] == CB_SELL) {
                    if (!can) { rect_fill(&c, bx, by, bw, WD_BTN_H, 0x04141a); rect_edge(&c, bx, by, bw, WD_BTN_H, MSP_DIM);
                                draw_text(&c, bx + (bw - text_w("SELL", sc)) / 2, by + (WD_BTN_H - 7 * sc) / 2, sc, MSP_DIM, "SELL"); }
                    else if (g_ms_sell_armed) { char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", worth); button(&c, bx, by, bw, WD_BTN_H, 0x155e58, 0x38dcc7, ok, text_w(ok, sc) + 8 <= bw ? sc : 2); }
                    else button(&c, bx, by, bw, WD_BTN_H, W_BTN_FILL, MSP_TEAL, "SELL", sc);
                } else if (id[b] == CB_GIVE) {
                    const bool dim = look == GV_DIM || look == GV_LEAVING;
                    if (dim) { rect_fill(&c, bx, by, bw, WD_BTN_H, 0x04141a); rect_edge(&c, bx, by, bw, WD_BTN_H, MSP_DIM);
                               const char *w = look == GV_LEAVING ? "GIVEN" : "GIVE"; draw_text(&c, bx + (bw - text_w(w, sc)) / 2, by + (WD_BTN_H - 7 * sc) / 2, sc, MSP_DIM, w); }
                    else button(&c, bx, by, bw, WD_BTN_H, look == GV_ASKING ? 0x155e58 : W_BTN_FILL, 0xe8c872, "GIVE", sc);
                } else button(&c, bx, by, bw, WD_BTN_H, W_BTN_FILL, MSP_TEAL, id[b] == CB_MOVE ? "MOVE" : "RENAME", sc);
            }
            /* the pane's three tabs: MILESTONES (its six badges, each with its words), STORY, TRAITS */
            for (int k = 0; k < WD_TAB_N; k++) {
                const int tx = WD_X + WD_TAB_X[k], tw = WD_TAB_W[k];
                if (k == g_w_tab) { rect_fill(&c, tx, WD_TAB_Y, tw, WD_TAB_H, MSP_TEAL); draw_text(&c, tx + (tw - text_w(WD_TAB_WORDS[k], 2)) / 2, WD_TAB_Y + (WD_TAB_H - 14) / 2, 2, MSP_INK, WD_TAB_WORDS[k]); }
                else button(&c, tx, WD_TAB_Y, tw, WD_TAB_H, W_BTN_FILL, MSP_TEAL, WD_TAB_WORDS[k], 2);
            }
            if (g_w_tab == 1) setup_big_story(t, g_w_slot, fb, stride, WD_X + 32, WD_LIST_Y + 12, WD_STORY_DY);
            else if (g_w_tab == 2) setup_big_traits(t, g_w_slot, fb, stride, WD_X + 32, WD_LIST_Y + 4, WD_FORM_DY, WD_LIST_Y + 4 + 6 * WD_FORM_DY + WD_NAT_GAP, WD_NAT_DY);
            else for (int k = 0; k < 6; k++) {
                uint32_t bit = FISH_BADGES[k].bit; bool on = (f->ms_bits & bit) != 0;
                int y = WD_LIST_Y + k * WD_LIST_DY;
                badge(&c, WD_X + 28, y, FISH_BADGES[k].icon, on, on && (f->ms_seen & bit) == 0);
                w_upper_word(MS_NAMES[bit_index(bit)], up, sizeof up);
                int sc = text_w(up, 3) <= WD_X + WD_W - 24 - (WD_X + 76) ? 3 : 2;   /* the long ones at the caption size */
                draw_text(&c, WD_X + 76, y + 2 + (sc == 2 ? 4 : 0), sc, on ? 0xffffff : MSP_DIM, up);
                draw_text(&c, WD_X + 76, y + 28, 2, on ? MSP_TEAL : 0x5f8a92, on ? "DONE" : "NOT YET");
            }
        }
    } else if (g_w_kind == WS_FRY) {
        int met = 0; for (int i = 0; i < nreq; i++) met += req[i].met;
        uint32_t fry_rgb = staged ? MSP_TEAL : MSP_DIM;
        render_fish_preview(fb, stride, CX - PAGE_X, WC_Y + 70 - PAGE_Y, 0.9f, fry_rgb, fry_rgb, fry_rgb, NULL, FISH_ROOM_CARD, t->clock);
        draw_text(&c, CX - text_w("NEW FRY", 4) / 2, WC_Y + 122, 4, staged ? 0xffffff : MSP_TEAL, "NEW FRY");
        if (staged) { draw_text(&c, CX - text_w("EVERY STEP IS DONE. A FRY", 2) / 2, WC_Y + 160, 2, MSP_TEAL, "EVERY STEP IS DONE. A FRY");
                      draw_text(&c, CX - text_w("WILL BE BORN IN THE GRASS", 2) / 2, WC_Y + 180, 2, MSP_TEAL, "WILL BE BORN IN THE GRASS"); }
        else { draw_text(&c, CX - text_w("WHEN ALL NEEDS ARE MET, A", 2) / 2, WC_Y + 160, 2, MSP_TEAL, "WHEN ALL NEEDS ARE MET, A");
               draw_text(&c, CX - text_w("NEW FRY IS READY TO BE BORN", 2) / 2, WC_Y + 180, 2, MSP_TEAL, "NEW FRY IS READY TO BE BORN"); }
        char tally[24]; snprintf(tally, sizeof tally, staged ? "ON ITS WAY" : "%d OF %d DONE", met, nreq);
        draw_text(&c, CX - text_w(tally, 3) / 2, WC_Y + 206, 3, 0xffffff, tally);
        /* the gates, each with its words and its bar; the HOW for one of them */
        int gy = WC_Y + 246, how = g_w_gate;
        if (how < 0 || how >= nreq || req[how].met) { how = -1; for (int k = 0; k < nreq && how < 0; k++) if (!req[k].met) how = k; }
        for (int k = 0; k < nreq; k++) {
            int y = gy + k * WD_GATE_DY;
            if (k == how) rect_edge(&c, WD_X + 16, y - 8, WD_W - 32, 56, MSP_TEAL);
            w_fry_badge(&c, fb, stride, t, WD_X + 28, y, &req[k]);
            draw_text(&c, WD_X + 76, y - 2, 3, req[k].met ? 0xffffff : MSP_DIM, req[k].title);
            draw_text(&c, WD_X + 76, y + 22, 2, req[k].met ? MSP_TEAL : 0x5f8a92, req[k].met ? "DONE" : req[k].progress);
            if (!req[k].met) {
                int w = (int)(140 * req[k].frac + 0.5f);
                rect_fill(&c, WD_X + WD_W - 40 - 140, y + 14, 140, 4, MSP_DIM);
                if (w > 0) rect_fill(&c, WD_X + WD_W - 40 - 140, y + 14, w, 4, MSP_TEAL);
            }
        }
        if (how >= 0) {                                                  /* HOW, inline: the gate's words, then its tip */
            int y = gy + nreq * WD_GATE_DY - 8 + WD_DIV_GAP;        /* (a gate's box ends 8 above the next gate's line) */
            for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(&c, x, y, MSP_DIM, 200);
            y += WD_DIV_GAP;
            char title[32]; snprintf(title, sizeof title, "%s: HOW", req[how].title);
            draw_text(&c, WD_X + 28, y, 3, 0xffffff, title); y += 28;
            if (req[how].words[0])  { draw_text(&c, WD_X + 28, y, 2, MSP_TEAL, req[how].words); y += 20; }
            if (req[how].words2[0]) { draw_text(&c, WD_X + 28, y, 2, MSP_TEAL, req[how].words2); y += 20; }
            const char *const *tip = progression_fry_tip(req[how].kind);
            for (int i = 0; tip && tip[i] && y < WC_Y + WC_H - 24; i++) { draw_text(&c, WD_X + 28, y + 4, 2, 0x9fb4b8, tip[i]); y += 20; }
        }
    } else if (g_w_kind == WS_TANK) {
        ms_school(t, fb, stride, WD_X - PAGE_X, WC_Y + 20 - PAGE_Y, WD_W);
        draw_text(&c, CX - text_w("TANK", 4) / 2, WC_Y + 112, 4, 0xffffff, "TANK");
        char line[40]; snprintf(line, sizeof line, "%d OF %d FISH SO FAR", t->n_fish, POP_CAP);
        draw_text(&c, CX - text_w(line, 2) / 2, WC_Y + 150, 2, MSP_TEAL, line);
        snprintf(line, sizeof line, "%d SAND DOLLARS", (int)t->sd_balance);
        int lw = text_w(line, 3) + 44;
        blit_icon(&c, CX - lw / 2, WC_Y + 172, &icon_ms_sand_dollar, 255);
        draw_text(&c, CX - lw / 2 + 44, WC_Y + 178, 3, 0xffffff, line);
        for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(&c, x, WC_Y + 226, MSP_DIM, 200);
        int nb = tank_badge_n(t);
        if (g_w_tank_k >= nb) g_w_tank_k = -1;
        for (int k = 0; k < nb; k++) {
            const badge_t *tb = tank_badge(t, k);
            int x = WD_GRID_X0 + (k % WD_GRID_COLS) * WD_GRID_DX, y = WD_GRID_Y + (k / WD_GRID_COLS) * WD_GRID_DY;
            g_ms_tshown |= tb->bit;
            if (k == g_w_tank_k) rect_edge(&c, x - 8, y - 8, MSP_ICON + 16, MSP_ICON + 16, 0xffffff);
            badge(&c, x, y, tb->icon, (t->tank_ms_bits & tb->bit) != 0, (t->tank_ms_seen & tb->bit) == 0);
        }
        int gy = WD_GRID_Y + ((nb + WD_GRID_COLS - 1) / WD_GRID_COLS) * WD_GRID_DY + 16;
        if (g_w_tank_k >= 0) {
            const badge_t *tb = tank_badge(t, g_w_tank_k); bool on = (t->tank_ms_bits & tb->bit) != 0;
            blit_icon_scaled(&c, CX - tb->icon->w, gy, tb->icon, 2, on);
            draw_text(&c, CX - text_w(w_upper_word(TMS_NAMES[bit_index(tb->bit)], up, sizeof up), 3) / 2, gy + 76, 3, on ? 0xffffff : MSP_DIM, up);
            draw_text(&c, CX - text_w(on ? "DONE" : "NOT YET", 2) / 2, gy + 104, 2, on ? MSP_TEAL : 0x5f8a92, on ? "DONE" : "NOT YET");
        } else draw_text(&c, CX - text_w("TAP A BADGE FOR ITS WORDS", 2) / 2, gy + 8, 2, 0x5f8a92, "TAP A BADGE FOR ITS WORDS");
    }
    {   /* the moving boxes, by the kept frame's own lines; then what this buffer holds */
        const bool in = w_anim_all(t, fb, stride, rows, n, req, nreq, staged);
        g_wk_n = -1;                                                   /* (this frame was drawn whole: the port turns it whole) */
        if (!g_ms_whole_dbg) { kp->fb = fb; kp->key = key; kp->ok = in && g_w_kind == WS_FISH; kp->frame = g_rframe; kp->other = g_ctx_other; }
    }
    g_ms_in = false;
}

int render_milestones_tap(const tank_t *t, float x, float y) {
    if (w_small_has_it(t)) return render_milestones_tap_small(t, x, y);
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    wrow_t rows[W_ROWS_MAX]; int n = w_rows(t, rows, W_ROWS_MAX, nreq);
    w_settle(t, rows, n);
    int rh = w_rail_hit(x, y);
    if (rh == WR_N) return MS_TAP_CLOSE;
    if (rh == WR_SETTINGS) return MS_TAP_SETTINGS;
    if (rh == WR_UPGRADES) return MS_TAP_SHOP;
    if (rh == WR_TANKS) return MS_TAP_TANKS;
    if (rh == WR_VISITORS) return collection_count() > 0 ? MS_TAP_VISITORS : MS_TAP_KEPT;
    if (rh == WR_KEEPER) return MS_TAP_KEPT;
    if (x >= WL_X && x < WL_X + WL_W) {                              /* the list: a row selects; its badge column too */
        int r = w_row_at(rows, n, y);
        if (r < 0) return MS_TAP_NONE;
        if (rows[r].kind == WS_BOWL) { bm_open_bowl(t, true); return MS_TAP_KEPT; }   /* THE WAITING ROW: the travel bowl's card */
        if (rows[r].kind == WS_TANK && x < WL_NAME_X - 8) return MS_TAP_SHOP;   /* the sand dollar: the shop, as on the small page */
        w_select(rows[r].kind, rows[r].slot);
        int k = x >= WL_BADGE_X0 - 4 ? (int)((x - WL_BADGE_X0 + 4) / WL_BADGE_DX) : -1;
        if (rows[r].kind == WS_TANK) g_w_tank_k = k >= 0 && k < tank_badge_n(t) ? k : -1;
        if (rows[r].kind == WS_FRY) g_w_gate = k >= 0 && k < nreq ? k : -1;
        return MS_TAP_KEPT;
    }
    if (x >= WD_X && x < WD_X + WD_W) {                              /* the pane */
        if (g_w_kind == WS_FISH && g_w_slot >= 0 && g_w_slot < t->n_fish) {
            const int by = WD_BTN_Y, cx = WD_X + WD_W / 2, fish = g_w_slot;
            if (w_evo_ready(t, fish) && x >= WD_EVO_X - WD_EVO_SLOP && y >= WD_EVO_Y - WD_EVO_SLOP && y < WD_EVO_Y + WD_EVO_H + WD_EVO_SLOP) {
                g_ms_sell_armed = false; return MS_TAP_EVOLVE + fish;   /* EVOLVE (0.4.3): the plate and a finger's reach round it (to the
                                                                           pane's edge); the platform opens the reveal */
            }
            if (y >= by - 12 && y < by + WD_BTN_H + 6) {
                int id[5]; const int nb = w_btns(t, fish, id); int b = 0;
                while (b < nb - 1 && x >= w_btn_x(nb, b + 1) - WD_BTNS_GAP / 2) b++;   /* each button's share of the row */
                const int look = give_look(t, fish);
                (void)cx;
                if (id[b] == CB_RENAME) { if (look == GV_ASKING) return MS_TAP_KEPT; g_w_kind = WS_NONE; return MS_TAP_RENAME + fish; }   /* the platform opens the wheel */
                if (id[b] == CB_GIVE) { give_tap(t, fish); return MS_TAP_KEPT; }          /* (its modal is the small page's: w_small_has_it) */
                if (id[b] == CB_MOVE) { render_move_tap(t, fish); return MS_TAP_KEPT; }   /* the travel bowl's card, or why not */
                if (!progression_fish_sellable(t, fish)) { g_ms_sell_asked = true; return MS_TAP_KEPT; }
                if (!g_ms_sell_armed) { g_ms_sell_armed = true; g_ms_sell_clock = t->clock; return MS_TAP_KEPT; }
                if (hold_spent_at(&g_ms_sell_clock, tap_press(t), t->clock, MSP_SELL_WAIT_S)) return MS_TAP_KEPT;   /* (THE TOUCH RULE, as the small card) */
                g_ms_sell_armed = false; g_w_kind = WS_NONE; return MS_TAP_SELL + fish;   /* the platform sells it; the pane settles on the first row */
            }
            g_ms_sell_armed = false;                                    /* a tap elsewhere disarms it */
            if (y >= WD_TAB_Y - 6 && y < WD_TAB_Y + WD_TAB_H + 8)       /* a tab (each takes the gap to its neighbour) */
                for (int k = 0; k < WD_TAB_N; k++) if (x < WD_X + WD_TAB_X[k] + WD_TAB_W[k] + 4 || k == WD_TAB_N - 1) { g_w_tab = k; break; }
            return MS_TAP_KEPT;
        }
        if (g_w_kind == WS_TANK) {
            int nb = tank_badge_n(t);
            int col = (int)((x - WD_GRID_X0 + 16) / WD_GRID_DX), row = (int)((y - WD_GRID_Y + 12) / WD_GRID_DY);
            if (col >= 0 && col < WD_GRID_COLS && row >= 0 && y >= WD_GRID_Y - 12) {
                int k = row * WD_GRID_COLS + col;
                g_w_tank_k = k < nb ? k : -1;
            }
            return MS_TAP_KEPT;
        }
        if (g_w_kind == WS_FRY) {
            int k = (int)((y - (WC_Y + 246) + 10) / WD_GATE_DY);
            if (y >= WC_Y + 236 && k >= 0 && k < nreq) g_w_gate = k;
            return MS_TAP_KEPT;
        }
        return MS_TAP_KEPT;
    }
    return MS_TAP_NONE;
}
bool render_milestones_swipe(const tank_t *t, float x, float y, float dx) { (void)t; (void)x; (void)y; (void)dx; return false; }
void render_milestones_leave(void) { g_w_kind = WS_NONE; g_w_slot = -1; g_w_tank_k = -1; g_w_gate = -1; g_ms_sell_armed = false; g_ms_sell_asked = false; g_w_tab = 0; render_milestones_leave_small(); }
void render_milestones_show_fish(const tank_t *t, int fish) { if (fish >= 0 && fish < t->n_fish) w_select(WS_FISH, fish); }
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y) {
    if (g_w_kind != WS_FISH || g_w_slot < 0 || g_w_slot >= t->n_fish) return false;
    int id[5]; const int nb = w_btns(t, g_w_slot, id), bw = w_btn_w(nb);
    if (fish) *fish = g_w_slot;
    for (int b = 0; b < nb; b++) {
        if (id[b] == CB_RENAME && rename_x) *rename_x = w_btn_x(nb, b) + bw / 2;
        if (id[b] == CB_SELL && sell_x) *sell_x = w_btn_x(nb, b) + bw / 2;
    }
    if (btn_y) *btn_y = WD_BTN_Y + WD_BTN_H / 2;
    return true;
}
bool render_bowl_row(int *x, int *y, int *w, int *h) {          /* THE WAITING ROW, where this page last drew it (frame px) */
    if (!progression_bowl_has() || g_w_bowl_top < 0) return false;
    if (x) *x = WL_X + 8;
    if (y) *y = g_w_bowl_top;
    if (w) *w = WL_W - 16;
    if (h) *h = WL_ROW_H;
    return true;
}
/* (the sim's tests) a fish's pane by slot, a visitor's too; its stage line as drawn; EVOLVE's box when the pane shows it */
void render_big_select_fish(int slot) { if (slot >= 0 && slot < N_FISH_SLOTS) w_select(WS_FISH, slot); }
const char *render_big_stage_line(const tank_t *t) {
    static char buf[40];
    if (g_w_kind != WS_FISH || g_w_slot < 0 || g_w_slot >= N_FISH_SLOTS || tank_is_visitor(g_w_slot)) return NULL;
    const char *s = w_stage_line(t, g_w_slot, buf, sizeof buf);
    if (s != buf) snprintf(buf, sizeof buf, "%s", s);
    return buf;
}
const char *render_big_list_word(const tank_t *t, int slot) {   /* (the tests) a home fish's word in the list */
    return slot >= 0 && slot < t->n_fish ? w_row_word(&t->fish[slot]) : NULL;
}
bool render_big_evolve_box(const tank_t *t, int *x, int *y, int *w, int *h) {
    if (g_w_kind != WS_FISH || !w_evo_ready(t, g_w_slot)) return false;
    *x = WD_EVO_X; *y = WD_EVO_Y; *w = WD_EVO_W; *h = WD_EVO_H;
    return true;
}
void render_big_tab(int k) { g_w_tab = k < 0 ? 0 : k >= WD_TAB_N ? WD_TAB_N - 1 : k; }   /* (the director, the sim's shots: a fish's pane on that tab) */
int render_milestones_changed(const short **xy) { *xy = g_wk_box; return g_wk_n; }   /* THE WIDE PAGE KEPT: this frame's moving boxes, or -1 = drawn whole */

/* ---- the UPGRADES page (the shop) ----
 * The pocket page's rows, a modal a row - here the modal is the DETAIL pane, always open on the picked row: the list
 * on the left (the coin and the balance, HOW TO EARN, the three areas as tabs, every row of the area - no pages), the
 * piece on the right with its words, its price or where it is, and its buttons. The answers are the pocket page's
 * (SHOP_TAP_*), so the platform buys, shelves, places and sells by the same lines; the rail's other pages answer
 * SHOP_TAP_TO_* (render.h, a big screen's alone). Asked twice, as there: a critter's SHELVE, a shelved piece's SELL.
 * THE WIDE PAGE KEPT holds it too: nothing moves but the fry (their row's portrait, their pane's three boxes). */
#define WSH_COIN_X  (WL_X + 24)
#define WSH_HEAD_X  (WL_X + 104)
#define WSH_EARN_W  172
#define WSH_EARN_H  44
#define WSH_EARN_X  (WL_X + WL_W - 24 - WSH_EARN_W)
#define WSH_EARN_Y  (WC_Y + 26)
#define WSH_DIV_Y   (WC_Y + 96)
#define WSH_TAB_Y   (WC_Y + 108)
#define WSH_TAB_H   40
static const short WSH_TAB_X[SHP_AREAS] = { 12, 180, 308 }, WSH_TAB_W[SHP_AREAS] = { 160, 120, 160 };
#define WSH_ROW_Y0  (WC_Y + 160)
#ifdef TANK_TAB5
#define WSH_ROW_H   44
#else
#define WSH_ROW_H   52
#endif
_Static_assert(WSH_ROW_Y0 + BG_N * WSH_ROW_H <= WC_Y + WC_H - 2, "the BACKGROUNDS do not fit the list: shorter rows, or pages");
#define WSH_NAME_X  (WL_X + 76)
#define WSH_ART_Y   (WC_Y + 32)        /* the pane: the piece at 4x */
#define WSH_NAME_Y  (WC_Y + 180)
#define WSH_WORDS_Y (WC_Y + 224)
#define WSH_PDIV_Y  (WC_Y + 282)
#define WSH_STATE_Y (WC_Y + 304)
#define WSH_BTN_Y   (WC_Y + 392)
#define WSH_BTN_H   56
#define WSH_ONE_W   240
#define WSH_TWO_W   200
#define WSH_TWO_GAP 24
#define WSH_FRY_W   132                /* the fry's pane: three boxes */
#define WSH_FRY_H   120
#define WSH_FRY_GAP 12
#define WSH_FRY_X(k) (WD_X + (WD_W - 3 * WSH_FRY_W - 2 * WSH_FRY_GAP) / 2 + (k) * (WSH_FRY_W + WSH_FRY_GAP))
#define WSH_FRY_Y   (WC_Y + 104)
static int  g_wsh_sel = -99;          /* the picked row's code: an item, SHOP_ROW_FRY, SHOP_ROW_BREED, SHOP_ROW_BG + a background */
static bool g_wsh_earn;               /* the pane shows HOW TO EARN */
static unsigned g_wsh_open;           /* how many times the page was left: the shelf's thumbs are built again at each opening, so a buffer that
                                         holds the page from before is not this one (the tile test: left and back in one frame) */
static bool g_big_pocket;         /* (the pocket page instead: the suites written for it) */
void render_big_pocket_pages(bool on) { g_big_pocket = on; }
static int wsh_settle(const tank_t *t, int *rows) {
    const int n = shp_list(t, rows);
    for (int i = 0; i < n; i++) if (rows[i] == g_wsh_sel) return n;
    g_wsh_sel = n ? rows[0] : -99;
    return n;
}
/* the pane's buttons, for the painter and the tap alike */
typedef struct { int n, x[3], w[3], code[3]; const char *lab[3]; bool live[3], prim[3], twice[3]; char buf[16]; } wsh_btns_t;
#define WSH_UP_S    (WSH_BTN_Y + WSH_BTN_H + 18)   /* A PIECE IN STAGES (0.4.3): the next stage's section, under the piece's own buttons */
#define WSH_UP_Y    (WSH_UP_S + 98)                  /* ... its price at the left, UPGRADE at the right */
static void wsh_btns(const tank_t *t, int sel, wsh_btns_t *b) {
    memset(b, 0, sizeof *b);
#define WSH_B(lab_, code_, live_, prim_, twice_) do { b->lab[b->n] = (lab_); b->code[b->n] = (code_); b->live[b->n] = (live_); b->prim[b->n] = (prim_); b->twice[b->n] = (twice_); b->n++; } while (0)
    if (sel == SHOP_ROW_FRY) {
        fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
        const bool go = progression_fry_room_to_buy(t) && t->sd_balance >= SD_PRICE_FRY && !pk.sold && progression_fry_left() > 0;
        WSH_B("BUY", SHOP_TAP_FRY + g_shp_fry_pick, go, true, false);
    } else if (sel == SHOP_ROW_BREED) {
        if (!progression_breed_owned(t)) WSH_B("UNLOCK", SHOP_TAP_BUY + SHOP_ITEM_BREED, t->sd_balance >= SD_PRICE_BREED, true, false);
    } else if (sel == SHOP_ROW_LAB) {                       /* THE FISH LAB (0.4.3): UNLOCK; once ours OPEN - none while it cannot pour (the pane says why) */
        if (!progression_lab_owned(t)) WSH_B("UNLOCK", SHOP_TAP_BUY + SHOP_ITEM_LAB, t->sd_balance >= SD_PRICE_LAB, true, false);
        else if (progression_lab_why(t) == LAB_OK) WSH_B("OPEN", SHOP_TAP_LAB, true, true, false);
    } else if (sel >= SHOP_ROW_BG) {
        const int bg = sel - SHOP_ROW_BG;
        if (t->bg_now == bg) { }
        else if (shp_bg_owned(t, bg)) WSH_B("USE", SHOP_TAP_BG_USE + bg, true, true, false);
        else WSH_B("UNLOCK", SHOP_TAP_BG_BUY + bg, t->sd_balance >= SD_PRICE_BG, true, false);
    } else if (sel >= 0 && sel < SD_ITEM_COUNT) {
        const sd_item_t *it = &SD_ITEMS[sel];
        const bool owned = (t->sd_unlocks & it->bit) != 0, shelved = owned && (t->sd_shelved & it->bit) != 0;
        const bool crit = (it->bit & SD_CRIT_BITS) != 0, full = crit && !tank_crit_room(t);
        if (owned && crit) {
            if (shelved) WSH_B("PUT BACK", SHOP_TAP_PLACE + sel, !full, true, false);
            else WSH_B(g_shp_sell_armed ? "SURE?" : "SHELVE", SHOP_TAP_SHELVE + sel, true, g_shp_sell_armed, true);
        } else if (owned && tank_decor_placeable(sel)) {
            if (shelved) {
                WSH_B("PLACE", SHOP_TAP_PLACE + sel, true, false, false);
                if (g_shp_sell_armed) snprintf(b->buf, sizeof b->buf, "+%d OK?", progression_sell_value(sel));
                WSH_B(g_shp_sell_armed ? b->buf : "SELL", SHOP_TAP_SELL + sel, true, g_shp_sell_armed, true);
            } else { WSH_B("MOVE", SHOP_TAP_MOVE + sel, true, false, false); WSH_B("SHELVE", SHOP_TAP_SHELVE + sel, true, false, false);
                     if (shp_upgradable(t, sel)) WSH_B("UPGRADE", SHOP_TAP_BUY + sel, t->sd_balance >= progression_next_price(t, sel), true, false); }
        } else if (!owned) WSH_B("UNLOCK", SHOP_TAP_BUY + sel, t->sd_balance >= it->price && !full, true, false);
    }
#undef WSH_B
    if (b->n == 1) { b->w[0] = WSH_ONE_W; b->x[0] = WD_X + (WD_W - WSH_ONE_W) / 2; }
    else for (int i = 0; i < b->n && i < 2; i++) { b->w[i] = WSH_TWO_W; b->x[i] = WD_X + (WD_W - 2 * WSH_TWO_W - WSH_TWO_GAP) / 2 + i * (WSH_TWO_W + WSH_TWO_GAP); }
    if (b->n == 3) { b->w[2] = WSH_TWO_W; b->x[2] = b->x[1]; }   /* (the UPGRADE: under SHELVE, its price beside it under MOVE) */
}
static int wsh_btn_y(const wsh_btns_t *b, int i) { return b->n == 3 && i == 2 ? WSH_UP_Y : WSH_BTN_Y; }
static void wsh_mid(ctx_t *c, int y, int scale, uint32_t rgb, const char *s) { draw_text(c, WD_X + (WD_W - text_w(s, scale)) / 2, y, scale, rgb, s); }
static void wsh_price(ctx_t *c, int y, int price, uint32_t rgb) {       /* the pane's price: the coin at 2x, the number large */
    char n[16]; snprintf(n, sizeof n, "%d", price);
    const int w = 32 + 10 + text_w(n, 4), x = WD_X + (WD_W - w) / 2;
    blit_icon_scaled(c, x, y, &icon_shop_sand_dollar_16, 2, true);
    draw_text(c, x + 42, y + 2, 4, rgb, n);
}
static uint32_t wsh_key(const tank_t *t, const int *rows, int n) {
    uint32_t h = 2166136261u;
    h = WK_MIX(h, 0x55); h = WK_MIX(h, g_wsh_open); h = WK_MIX(h, g_shp_area); h = WK_MIX(h, g_wsh_sel); h = WK_MIX(h, g_wsh_earn); h = WK_MIX(h, g_shp_sell_armed); h = WK_MIX(h, g_shp_fry_pick);
    h = WK_MIX(h, (int)t->sd_balance); h = WK_MIX(h, t->sd_unlocks); h = WK_MIX(h, t->sd_shelved); h = WK_MIX(h, t->bg_now); h = WK_MIX(h, n);
    h = WK_MIX(h, collection_count() > 0); h = WK_MIX(h, tank_crit_room(t)); h = WK_MIX(h, progression_breed_owned(t)); h = WK_MIX(h, progression_fry_room_to_buy(t));
    h = WK_MIX(h, progression_stage(t, 2)); h = WK_MIX(h, progression_lab_owned(t)); h = WK_MIX(h, progression_lab_why(t));
    h = WK_MIX(h, progression_fish_held(t)); h = WK_MIX(h, tank_light_off(t)); h = WK_MIX(h, progression_fry_left()); h = WK_MIX(h, g_cjar_built);
    for (int i = 0; i < n; i++) h = WK_MIX(h, rows[i]);
    for (int i = 0; i < SD_ITEM_COUNT; i++) {
        h = WK_MIX(h, tank_decor_locked(t, i));
        if (shelf_idx(i) >= 0 && (t->sd_shelved & SD_ITEMS[i].bit)) { h = WK_MIX(h, shelf_key(t, i)); h = WK_MIX(h, g_shelf_key[shelf_idx(i)]); }
    }
    if (g_shp_area == SHP_AREA_CRITTERS) {
        for (int k = 0; k < FRY_OFFER_N; k++) { fry_offer_t o; progression_fry_offer(k, &o); h = WK_MIX(h, o.sold); h = WK_MIX(h, o.body); h = WK_MIX(h, o.accent); h = WK_MIX(h, o.shape); h = WK_MIX(h, o.fins); h = WK_MIX(h, o.pattern); }
        char l[32]; fry_hours(l, sizeof l); for (const char *q = l; *q; q++) h = WK_MIX(h, *q);
        if (g_shp_sell_armed && g_wsh_sel >= 0 && g_wsh_sel < SD_ITEM_COUNT) { char sub[40]; crit_waits(t, g_wsh_sel, sub, sizeof sub); for (const char *q = sub; *q; q++) h = WK_MIX(h, *q); }
    }
    return h;
}
/* the moving boxes: the fry in their row, the fry in their pane's boxes; false = one swam out of its cell */
static bool wsh_anim_all(const tank_t *t, uint16_t *fb, int stride, const int *rows, int n) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    bool in = true; g_wk_n = 0;
    if (g_shp_area != SHP_AREA_CRITTERS) return true;
    for (int r = 0; r < n; r++) if (rows[r] == SHOP_ROW_FRY) {
        const int top = WSH_ROW_Y0 + r * WSH_ROW_H, k = fry_first_left();
        if (k < 0) break;
        fry_offer_t o; progression_fry_offer(k, &o);
        WK_BOX(WL_X + 10, top + 1, WL_X + 70, top + WSH_ROW_H - 4, g_wsh_sel == SHOP_ROW_FRY ? W_BTN_FILL : W_PANE_BG,
               fry_draw(fb, stride, WL_X + 40 - PAGE_X, top + WSH_ROW_H / 2 - 1 - PAGE_Y, 0.9f, &o, t->clock));
    }
    if (g_wsh_sel == SHOP_ROW_FRY && !g_wsh_earn)
        for (int k = 0; k < FRY_OFFER_N; k++) {
            fry_offer_t o; progression_fry_offer(k, &o);
            if (o.sold) continue;
            const int bx = WSH_FRY_X(k), by = WSH_FRY_Y;
            WK_BOX(bx + 2, by + 2, bx + WSH_FRY_W - 3, by + WSH_FRY_H - 30, k == g_shp_fry_pick ? 0x14303a : 0x0b1d23,
                   fry_draw(fb, stride, bx + WSH_FRY_W / 2 - PAGE_X, by + 46 - PAGE_Y, 2.0f, &o, t->clock + k * 0.7f));
        }
    return in;
}
static void wsh_row(ctx_t *c, const tank_t *t, int code, int top) {
    const int ix = WL_X + 24, iy = top + (WSH_ROW_H - 34) / 2;
    const char *name, *sub = NULL; int price = -1, stages = -1; bool can = false; char buf[32];
    if (code == SHOP_ROW_FRY) {
        const int k = fry_first_left();
        name = "FRY";
        if (k < 0) { fry_hours(buf, sizeof buf); sub = buf; }
        else { price = SD_PRICE_FRY; can = t->sd_balance >= SD_PRICE_FRY && progression_fry_room_to_buy(t); }
    } else if (code == SHOP_ROW_BREED) {
        const bool owned = progression_breed_owned(t);
        if (owned) blit_icon(c, ix, iy, &icon_shop_breed, 255); else blit_icon_locked(c, ix, iy, &icon_shop_breed);
        name = BREED_NAME;
        if (owned) sub = "IN THE TOOLBOX"; else { price = SD_PRICE_BREED; can = t->sd_balance >= SD_PRICE_BREED; }
    } else if (code == SHOP_ROW_LAB) {                      /* THE FISH LAB (0.4.3): its price; once ours, a fry's */
        const bool owned = progression_lab_owned(t);
        lab_icon(c, ix, iy, 1, owned);
        name = LAB_NAME;
        if (!owned) { price = SD_PRICE_LAB; can = t->sd_balance >= SD_PRICE_LAB; }
        else if (progression_lab_why(t) == LAB_FULL) sub = "TANK FULL";
        else { price = SD_PRICE_LAB_FRY; can = progression_lab_why(t) == LAB_OK; }
    } else if (code >= SHOP_ROW_BG) {
        const int bg = code - SHOP_ROW_BG;
        big_bg_thumb(c, ix, iy + (32 - BGT_SMALL_H) / 2, bg, false);   /* (the wide scene's own picture, at this size: bg_thumbs_big.inc) */
        name = bg_name(bg);
        if (t->bg_now == bg) sub = "IN USE"; else if (shp_bg_owned(t, bg)) sub = "OWNED"; else { price = SD_PRICE_BG; can = t->sd_balance >= SD_PRICE_BG; }
    } else {
        const sd_item_t *it = &SD_ITEMS[code];
        const bool owned = (t->sd_unlocks & it->bit) != 0, shelved = owned && (t->sd_shelved & it->bit);
        if (shelved) shelf_slot(c, ix, iy, t, code, 1);
        else if (owned) blit_icon(c, ix, iy, shop_icon(code), 255); else blit_icon_locked(c, ix, iy, shop_icon(code));
        name = it->name;
        if (shelved) sub = "ON THE SHELF";
        else if (shp_upgradable(t, code)) { price = progression_next_price(t, code); can = t->sd_balance >= price; }   /* (the next stage's price) */
        else if (owned) sub = "IN THE TANK";
        else { price = it->price; can = t->sd_balance >= it->price && !((it->bit & SD_CRIT_BITS) && !tank_crit_room(t)); }
        if (owned && !shelved && progression_stages(code) > 1) stages = code;
    }
    /* THE PRICE AT THE ROW'S RIGHT (Strato 11-21: under the name it "feels crowded and does not allow for much padding
       above and below. there is plenty of real estate to the right"): the name alone in the row's middle, the price - or
       where the piece is - against the right edge. A name too long to stand beside it goes on two smaller lines, broken
       at its last space (SELECTIVE BREEDING; STRAWBERRY CRAB beside IN THE TANK) */
    const int rx = WL_X + WL_W - 24, room = rx - WSH_NAME_X;
    char pn[16] = ""; int rw;
    if (sub) rw = text_w(sub, 2); else { snprintf(pn, sizeof pn, "%d", price); rw = 20 + text_w(pn, 2); }
    const char *sp = strrchr(name, ' ');
    if (stages >= 0) {                                      /* A PIECE IN STAGES: its squares after its name (filled = a stage owned: by shape, not colour) */
        draw_text(c, WSH_NAME_X, top + (WSH_ROW_H - 2 - 21) / 2, 3, 0xffffff, name);
        shp_stage_squares(c, WSH_NAME_X + text_w(name, 3) + 14, top + (WSH_ROW_H - 2 - 12) / 2, 12, t, stages);
    } else
    if (text_w(name, 3) + 16 + rw <= room || !sp) draw_text(c, WSH_NAME_X, top + (WSH_ROW_H - 2 - 21) / 2, 3, 0xffffff, name);
    else {
        char l1[24]; snprintf(l1, sizeof l1, "%.*s", (int)(sp - name), name);
        const int y0 = top + (WSH_ROW_H - 2 - 34) / 2;
        draw_text(c, WSH_NAME_X, y0, 2, 0xffffff, l1); draw_text(c, WSH_NAME_X, y0 + 20, 2, 0xffffff, sp + 1);
    }
    const int ry = top + (WSH_ROW_H - 2 - 14) / 2;
    if (sub) draw_text(c, rx - rw, ry, 2, code == SHOP_ROW_FRY ? MSP_QUIET : MSP_TEAL, sub);
    else price_tag(c, rx - rw, ry, price, can ? MSP_TEAL : MSP_QUIET);
}
static void wsh_pane(ctx_t *c, const tank_t *t) {
    const int CX = WD_X + WD_W / 2, sel = g_wsh_sel;
    char line[40];
    if (g_wsh_earn) {
        wsh_mid(c, WC_Y + 40, 4, 0xffffff, "HOW TO EARN");
        for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(c, x, WC_Y + 88, MSP_DIM, 200);
        const char *const *lines = progression_sd_earn_lines();
        for (int i = 0; lines[i]; i++) draw_text(c, WD_X + 40, WC_Y + 112 + i * 32, 2, MSP_TEAL, lines[i]);
        return;
    }
    if (sel == SHOP_ROW_FRY) {
        wsh_mid(c, WC_Y + 40, 4, 0xffffff, "FRY FOR SALE");
        for (int k = 0; k < FRY_OFFER_N; k++) {
            fry_offer_t o; progression_fry_offer(k, &o);
            const int bx = WSH_FRY_X(k), by = WSH_FRY_Y;
            const bool lit = !o.sold && k == g_shp_fry_pick;
            rect_fill(c, bx, by, WSH_FRY_W, WSH_FRY_H, lit ? 0x14303a : 0x0b1d23);
            const uint32_t e = lit ? 0xffffff : o.sold ? 0x1c2f36 : 0x4d6a72;
            rect_edge(c, bx, by, WSH_FRY_W, WSH_FRY_H, e);
            if (lit) rect_edge(c, bx + 1, by + 1, WSH_FRY_W - 2, WSH_FRY_H - 2, e);
            if (o.sold) continue;
            draw_text(c, bx + (WSH_FRY_W - text_w(SHAPE_NAMES[o.shape], 2)) / 2, by + WSH_FRY_H - 24, 2, lit ? 0xffffff : 0x9fd8e2, SHAPE_NAMES[o.shape]);
        }
        const bool room = progression_fry_room_to_buy(t), can = t->sd_balance >= SD_PRICE_FRY;
        fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
        const bool go = room && can && !pk.sold && progression_fry_left() > 0;
        for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(c, x, WSH_FRY_Y + WSH_FRY_H + 22, MSP_DIM, 200);
        wsh_price(c, WSH_FRY_Y + WSH_FRY_H + 44, SD_PRICE_FRY, go ? 0xffffff : MSP_QUIET);
        if (!room) snprintf(line, sizeof line, progression_fish_held(t) >= POP_CAP ? "THE TANK IS FULL" : tank_light_off(t) ? "LIGHT ON TO NAME THE FRY" : "WELCOME THE NEW FRY");
        else if (!can) snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
        else line[0] = 0;
        if (line[0]) wsh_mid(c, WSH_FRY_Y + WSH_FRY_H + 90, 2, MSP_QUIET, line);
        fry_hours(line, sizeof line);
        wsh_mid(c, WSH_BTN_Y + WSH_BTN_H + 20, 2, MSP_QUIET, line);
    } else {
        const char *name, *w1, *w2; const icon_t *ic; bool lit = true, slot = false;
        if (sel == SHOP_ROW_LAB) {                           /* THE FISH LAB (0.4.3) */
            const bool owned = progression_lab_owned(t), can = t->sd_balance >= SD_PRICE_LAB; const int why = owned ? progression_lab_why(t) : LAB_OK;
            lab_icon(c, CX - 64, WSH_ART_Y, 4, true);
            wsh_mid(c, WSH_NAME_Y, 4, 0xffffff, LAB_NAME); wsh_mid(c, WSH_WORDS_Y, 2, MSP_TEAL, LAB_W1); wsh_mid(c, WSH_WORDS_Y + 24, 2, MSP_TEAL, LAB_W2);
            for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(c, x, WSH_PDIV_Y, MSP_DIM, 200);
            if (owned) { wsh_price(c, WSH_STATE_Y, SD_PRICE_LAB_FRY, why == LAB_OK ? 0xffffff : MSP_QUIET); wsh_mid(c, WSH_STATE_Y + 44, 2, MSP_TEAL, "EACH FRY"); }
            else wsh_price(c, WSH_STATE_Y, SD_PRICE_LAB, can ? 0xffffff : MSP_QUIET);
            const int wy = WSH_STATE_Y + (owned ? 66 : 46);
            if (why == LAB_FULL) wsh_mid(c, wy, 2, 0xffffff, "YOUR TANK IS FULL");
            else if ((owned && why == LAB_SHORT) || (!owned && !can)) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance); wsh_mid(c, wy, 2, MSP_QUIET, line); }
            goto buttons;
        }
        if (sel == SHOP_ROW_BREED) { name = BREED_NAME; w1 = BREED_W1; w2 = BREED_W2; ic = &icon_shop_breed; }
        else if (sel >= SHOP_ROW_BG) { name = bg_name(sel - SHOP_ROW_BG); bg_words(sel - SHOP_ROW_BG, &w1, &w2); ic = bg_thumb(sel - SHOP_ROW_BG); }
        else if (sel >= 0 && sel < SD_ITEM_COUNT) { const sd_item_t *it = &SD_ITEMS[sel]; name = it->name; w1 = it->words; w2 = it->words2; ic = shop_icon(sel);
                                                    slot = (t->sd_unlocks & it->bit) && (t->sd_shelved & it->bit);
                                                    if (progression_stages(sel) > 1) { const int st = progression_stage(t, sel);   /* (a staged piece: the stage it is at) */
                                                        w1 = progression_stage_words(sel, st ? st : 1, 0); w2 = progression_stage_words(sel, st ? st : 1, 1); } }
        else return;
        (void)lit;
        if (slot && crit_jar_idx(sel) >= 0) shelf_slot(c, CX - 32, WSH_ART_Y + 32, t, sel, 2);   /* on the shelf: a critter in its jar (painted live at that size) */

        if (slot && shelf_idx(sel) >= 0 && g_shelf_key[shelf_idx(sel)]) for (int i = 1; i < 12; i++) px_blend(c, CX - 64 + 8 + i, WSH_ART_Y + 9, 0xffffff, 110);   /* (its glint, two px thick at this size) */
        else if (sel >= SHOP_ROW_BG) big_bg_thumb(c, CX - BGT_BIG_W / 2, WSH_ART_Y + (128 - BGT_BIG_H) / 2, sel - SHOP_ROW_BG, true);   /* (drawn at this size, not a 32 px icon four for one: Strato 11-21, "pixelated on the P4") */
        else blit_icon_scaled(c, CX - 64, WSH_ART_Y, ic, 4, true);
        wsh_mid(c, WSH_NAME_Y, 4, 0xffffff, name);
        wsh_mid(c, WSH_WORDS_Y, 2, MSP_TEAL, w1);
        wsh_mid(c, WSH_WORDS_Y + 24, 2, MSP_TEAL, w2);
        for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(c, x, WSH_PDIV_Y, MSP_DIM, 200);
        const int Y = WSH_STATE_Y;
        if (sel == SHOP_ROW_BREED) {
            const bool can = t->sd_balance >= SD_PRICE_BREED;
            if (progression_breed_owned(t)) wsh_mid(c, Y, 3, MSP_TEAL, "IN THE TOOLBOX");
            else { wsh_price(c, Y, SD_PRICE_BREED, can ? 0xffffff : MSP_QUIET);
                   if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance); wsh_mid(c, Y + 46, 2, MSP_QUIET, line); } }
        } else if (sel >= SHOP_ROW_BG) {
            const int bg = sel - SHOP_ROW_BG; const bool can = t->sd_balance >= SD_PRICE_BG;
            if (t->bg_now == bg) wsh_mid(c, Y, 3, MSP_TEAL, "IN USE");
            else if (shp_bg_owned(t, bg)) wsh_mid(c, Y, 3, MSP_TEAL, "OWNED");
            else { wsh_price(c, Y, SD_PRICE_BG, can ? 0xffffff : MSP_QUIET);
                   if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance); wsh_mid(c, Y + 46, 2, MSP_QUIET, line); } }
        } else {
            const sd_item_t *it = &SD_ITEMS[sel];
            const bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price, shelved = owned && (t->sd_shelved & it->bit);
            const bool crit = (it->bit & SD_CRIT_BITS) != 0, full = crit && !tank_crit_room(t);
            _Static_assert(CRIT_CAP == 5, "the words say FIVE CRITTERS: change them with the cap");
            if ((!owned || shelved) && full) {                             /* a critter past the cap: why its button is dim */
                wsh_mid(c, Y, 2, 0xffffff, "FIVE CRITTERS IS THE MOST");
                wsh_mid(c, Y + 26, 2, MSP_QUIET, "SHELVE ONE TO MAKE ROOM");
            } else if (owned) {
                wsh_mid(c, Y, 3, MSP_TEAL, shelved ? "ON THE SHELF" : "IN THE TANK");
                const char *sub = NULL;
                if (crit) { if (!shelved && g_shp_sell_armed) sub = crit_waits(t, sel, line, sizeof line); }
                else if (tank_decor_placeable(sel)) sub = shelf_line(t, sel, line, sizeof line);
                if (sub) wsh_mid(c, Y + 36, 2, crit ? 0xffffff : MSP_QUIET, sub);
                if (shp_upgradable(t, sel)) {                   /* A PIECE IN STAGES: the next stage, under its own buttons - which stage, what it adds;
                                                                   its price at the left and UPGRADE at the right, on one line (the Tab5's 720 rows) */
                    const int st = progression_stage(t, sel), price = progression_next_price(t, sel); const bool ok = t->sd_balance >= price;
                    for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(c, x, WSH_UP_S, MSP_DIM, 200);
                    snprintf(line, sizeof line, "STAGE %d OF %d", st + 1, progression_stages(sel));
                    { const int sqw = progression_stages(sel) * 18 - 4, tw = text_w(line, 3), x0 = WD_X + (WD_W - tw - 16 - sqw) / 2;
                      draw_text(c, x0, WSH_UP_S + 14, 3, 0xffffff, line); shp_stage_squares(c, x0 + tw + 16, WSH_UP_S + 18, 14, t, sel); }
                    wsh_mid(c, WSH_UP_S + 46, 2, MSP_TEAL, progression_stage_words(sel, st + 1, 0));
                    wsh_mid(c, WSH_UP_S + 68, 2, MSP_TEAL, progression_stage_words(sel, st + 1, 1));
                    {   char n[16]; snprintf(n, sizeof n, "%d", price);   /* the price, under MOVE: the coin at 2x, the number at 3 */
                        const int cx0 = WD_X + (WD_W - 2 * WSH_TWO_W - WSH_TWO_GAP) / 2, w = 32 + 10 + text_w(n, 3), x = cx0 + (WSH_TWO_W - w) / 2;
                        blit_icon_scaled(c, x, WSH_UP_Y + (WSH_BTN_H - 32) / 2, &icon_shop_sand_dollar_16, 2, true);
                        draw_text(c, x + 42, WSH_UP_Y + (WSH_BTN_H - 21) / 2, 3, ok ? 0xffffff : MSP_QUIET, n); }
                    if (!ok) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance); wsh_mid(c, WSH_UP_Y + WSH_BTN_H + 14, 2, MSP_QUIET, line); }
                }
            } else {
                wsh_price(c, Y, it->price, can ? 0xffffff : MSP_QUIET);
                if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance); wsh_mid(c, Y + 46, 2, MSP_QUIET, line); }
            }
        }
    }
buttons:;
    wsh_btns_t b; wsh_btns(t, sel, &b);
    for (int i = 0; i < b.n; i++) {
        const int by = wsh_btn_y(&b, i);
        if (b.live[i] && b.prim[i]) { button(c, b.x[i], by, b.w[i], WSH_BTN_H, MSP_TEAL, MSP_TEAL, "", 3);
                                      draw_text(c, b.x[i] + (b.w[i] - text_w(b.lab[i], 3)) / 2, by + (WSH_BTN_H - 21) / 2, 3, MSP_INK, b.lab[i]); }
        else button(c, b.x[i], by, b.w[i], WSH_BTN_H, W_BTN_FILL, b.live[i] ? MSP_TEAL : MSP_DIM, b.lab[i], 3);
    }
}
void render_shop(const tank_t *t, uint16_t *fb, int stride) {
    if (g_big_pocket) { g_wk[0].ok = g_wk[1].ok = false; g_wk_n = -1; render_shop_small(t, fb, stride); return; }
    int rows[16]; const int n = wsh_settle(t, rows);
    const uint32_t key = wsh_key(t, rows, n);
    if (!g_ms_whole_dbg) {
        if (g_wk_frame != g_rframe && g_wk_frame + 1 != g_rframe) g_wk_run0 = g_rframe;   /* a frame without a kept page: a new run */
        g_wk_frame = g_rframe;
    }
    wk_t *kp = g_wk[0].fb == fb ? &g_wk[0] : g_wk[1].fb == fb ? &g_wk[1] : g_wk[0].frame <= g_wk[1].frame ? &g_wk[0] : &g_wk[1];
    const bool keep = !g_ms_whole_dbg && kp->fb == fb && kp->ok && kp->key == key && kp->other == g_ctx_other && stride == TANK_W
                      && kp->frame < g_rframe && kp->frame >= g_wk_run0;
    g_ms_in = true; g_dirty_hold = true; g_wk_n = -1;
    if (keep && wsh_anim_all(t, fb, stride, rows, n)) { kp->frame = g_rframe; g_wk_kept_n++; g_ms_in = false; return; }
    g_wk_whole_n++; g_wk_n = -1;
    shelf_prepare(t, fb, stride);                             /* (the shelf's thumbs first: the frame is their scratch) */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    w_rail(&c, WR_UPGRADES, collection_count() > 0);
    /* the list: the coin, HOW TO EARN, the areas, the rows */
    w_pane(&c, WL_X, WL_W, 0x2c4a52);
    blit_icon(&c, WSH_COIN_X, WC_Y + 16, &icon_shop_sand_dollar_64, 255);
    draw_text(&c, WSH_HEAD_X, WC_Y + 22, 2, MSP_TEAL, "SAND DOLLARS");
    { char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance); draw_text(&c, WSH_HEAD_X, WC_Y + 46, 4, 0xffffff, bal); }
    if (g_wsh_earn) { button(&c, WSH_EARN_X, WSH_EARN_Y, WSH_EARN_W, WSH_EARN_H, MSP_TEAL, MSP_TEAL, "", 2);
                      draw_text(&c, WSH_EARN_X + (WSH_EARN_W - text_w("HOW TO EARN", 2)) / 2, WSH_EARN_Y + (WSH_EARN_H - 14) / 2, 2, MSP_INK, "HOW TO EARN"); }
    else button(&c, WSH_EARN_X, WSH_EARN_Y, WSH_EARN_W, WSH_EARN_H, W_BTN_FILL, MSP_TEAL, "HOW TO EARN", 2);
    for (int x = WL_X + 24; x < WL_X + WL_W - 24; x++) px_blend(&c, x, WSH_DIV_Y, MSP_DIM, 200);
    for (int a = 0; a < SHP_AREAS; a++) {
        const int tx = WL_X + WSH_TAB_X[a], tw = WSH_TAB_W[a];
        if (a == g_shp_area) { button(&c, tx, WSH_TAB_Y, tw, WSH_TAB_H, MSP_TEAL, MSP_TEAL, "", 2);
                               draw_text(&c, tx + (tw - text_w(SHP_AREA_NAME[a], 2)) / 2, WSH_TAB_Y + (WSH_TAB_H - 14) / 2, 2, MSP_INK, SHP_AREA_NAME[a]); }
        else button(&c, tx, WSH_TAB_Y, tw, WSH_TAB_H, W_BTN_FILL, MSP_TEAL, SHP_AREA_NAME[a], 2);
    }
    for (int r = 0; r < n; r++) {
        const int top = WSH_ROW_Y0 + r * WSH_ROW_H;
        if (rows[r] == g_wsh_sel && !g_wsh_earn) { rect_fill(&c, WL_X + 8, top, WL_W - 16, WSH_ROW_H - 2, W_BTN_FILL); rect_edge(&c, WL_X + 8, top, WL_W - 16, WSH_ROW_H - 2, MSP_TEAL); }
        wsh_row(&c, t, rows[r], top);
    }
    /* the pane */
    w_pane(&c, WD_X, WD_W, MSP_TEAL);
    wsh_pane(&c, t);
    {   const bool in = wsh_anim_all(t, fb, stride, rows, n);
        g_wk_n = -1;                                                   /* (this frame was drawn whole: the port turns it whole) */
        if (!g_ms_whole_dbg) { kp->fb = fb; kp->key = wsh_key(t, rows, n); kp->ok = in; kp->frame = g_rframe; kp->other = g_ctx_other; }   /* (the key again: the thumbs were built) */
    }
    g_ms_in = false;
}
int render_shop_tap(const tank_t *t, float x, float y) {
    if (g_big_pocket) return render_shop_tap_small(t, x, y);
    int rows[16]; const int n = wsh_settle(t, rows);
    const int rh = w_rail_hit(x, y);
    if (rh >= 0) g_shp_sell_armed = false;
    if (rh == WR_N) return SHOP_TAP_TO_TANK;
    if (rh == WR_KEEPER) return SHOP_TAP_CLOSE;                  /* back to the keeper page, as the pocket page's X */
    if (rh == WR_SETTINGS) return SHOP_TAP_TO_SETTINGS;
    if (rh == WR_TANKS) return SHOP_TAP_TO_TANKS;
    if (rh == WR_VISITORS) return collection_count() > 0 ? SHOP_TAP_TO_VISITORS : SHOP_TAP_KEPT;
    if (rh == WR_UPGRADES) return SHOP_TAP_KEPT;
    if (x >= WL_X && x < WL_X + WL_W) {
        g_shp_sell_armed = false;
        if (y < WSH_DIV_Y) { if (x >= WSH_EARN_X - 16) g_wsh_earn = !g_wsh_earn; return SHOP_TAP_KEPT; }
        if (y < WSH_ROW_Y0 - 4) {                                  /* the areas (each takes the gap to its neighbour) */
            for (int a = 0; a < SHP_AREAS; a++) if (x < WL_X + WSH_TAB_X[a] + WSH_TAB_W[a] + 4 || a == SHP_AREAS - 1) {
                if (g_shp_area != a) { g_shp_area = a; g_wsh_sel = -99; }
                g_wsh_earn = false; break; }
            return SHOP_TAP_KEPT;
        }
        const int r = (int)((y - WSH_ROW_Y0) / WSH_ROW_H);
        if (r < 0 || r >= n) return SHOP_TAP_NONE;
        if (rows[r] == SHOP_ROW_FRY && g_wsh_sel != SHOP_ROW_FRY) { const int k = fry_first_left(); g_shp_fry_pick = k < 0 ? 0 : k; }
        g_wsh_sel = rows[r]; g_wsh_earn = false;
        return SHOP_TAP_KEPT;
    }
    if (x >= WD_X && x < WD_X + WD_W) {
        if (g_wsh_earn) { g_wsh_earn = false; return SHOP_TAP_KEPT; }
        if (g_wsh_sel == SHOP_ROW_FRY && y >= WSH_FRY_Y - 8 && y < WSH_FRY_Y + WSH_FRY_H + 8) {
            for (int k = 0; k < FRY_OFFER_N; k++)
                if (x >= WSH_FRY_X(k) - WSH_FRY_GAP / 2 && x < WSH_FRY_X(k) + WSH_FRY_W + WSH_FRY_GAP / 2) { fry_offer_t o; progression_fry_offer(k, &o); if (!o.sold) g_shp_fry_pick = k; }
            return SHOP_TAP_KEPT;
        }
        wsh_btns_t b; wsh_btns(t, g_wsh_sel, &b);
        for (int i = 0; i < b.n; i++) {
                const int by = wsh_btn_y(&b, i);
                if (y < by - 14 || y >= by + WSH_BTN_H + 14) continue;
                const int slop = b.n == 1 || (b.n == 3 && i == 2) ? 30 : WSH_TWO_GAP / 2;
                if (x < b.x[i] - slop || x >= b.x[i] + b.w[i] + slop) continue;
                if (!b.live[i]) { g_shp_sell_armed = false; return SHOP_TAP_KEPT; }
                if (b.twice[i]) {
                    if (!g_shp_sell_armed) { g_shp_sell_armed = true; g_shp_sell_clock = t->clock; return SHOP_TAP_KEPT; }
                    if (hold_spent_at(&g_shp_sell_clock, tap_press(t), t->clock, MSP_SELL_WAIT_S)) return SHOP_TAP_KEPT;   /* a doubled tap: still armed (THE TOUCH RULE) */
                }
                g_shp_sell_armed = false;
                return b.code[i];
            }
        g_shp_sell_armed = false;                                    /* a tap elsewhere disarms it */
        return SHOP_TAP_KEPT;
    }
    return SHOP_TAP_NONE;
}
void render_shop_leave(void) { g_wsh_sel = -99; g_wsh_earn = false; g_wsh_open++; render_shop_leave_small(); }
void render_big_shop_pick(const tank_t *t, int area, int code, bool earn) {   /* (the director, the sim's shots: that area, that row picked) */
    g_shp_area = area < 0 ? 0 : area >= SHP_AREAS ? SHP_AREAS - 1 : area; g_wsh_sel = code; g_wsh_earn = earn; g_shp_sell_armed = false;
    if (code == SHOP_ROW_FRY) { const int k = fry_first_left(); g_shp_fry_pick = k < 0 ? 0 : k; }
    (void)t;
}
/* where the page's controls are, frame px (the tests): a row's middle, the pane's button i */
bool render_big_shop_where(const tank_t *t, int code, int btn, int *x, int *y) {
    int rows[16]; const int n = shp_list(t, rows);
    if (btn < 0) { for (int r = 0; r < n; r++) if (rows[r] == code) { *x = WL_X + WL_W / 2; *y = WSH_ROW_Y0 + r * WSH_ROW_H + WSH_ROW_H / 2; return true; } return false; }
    wsh_btns_t b; wsh_btns(t, g_wsh_sel, &b);
    if (btn >= b.n) return false;
    *x = b.x[btn] + b.w[btn] / 2; *y = wsh_btn_y(&b, btn) + WSH_BTN_H / 2; return true;
}
void render_big_shop_tab(int area, int *x, int *y) { *x = WL_X + WSH_TAB_X[area] + WSH_TAB_W[area] / 2; *y = WSH_TAB_Y + WSH_TAB_H / 2; }
#undef WK_BOX

/* ---- the SETTINGS page ----
 * One wide pane, two columns. SCREEN AND SOUND: BRIGHTNESS and VOLUME (the pocket page's sliders, longer), LIGHTS OUT
 * (a value between two arrows), VIGNETTE (the pocket tanks' GRAPHICS page has it; here it is a row - that page's
 * QUALITY has nothing to act on while a big screen draws its grass live, and is not shown). TANK: AUTO FEED, UPDATES,
 * LINK. ROTATION is not shown: these panels do not turn (display_port_quarters). The press / drag / release rules are
 * the pocket page's own (render_settings_touch there): a slider follows the finger and is kept where it is let go, a
 * button is a tap that stayed on what it pressed. The answers are SET_TAP_*; the rail's other pages SET_TAP_TO_*.
 * THE WIDE PAGE KEPT: the two sliders are its moving boxes (a drag redraws two strips, not a megapixel). */
#ifdef TANK_TAB5
#define WST_PITCH   124
#else
#define WST_PITCH   140
#endif
#define WST_LX      (WC_X + 36)        /* the left column */
#define WST_LW      440
#define WST_RX      (WC_X + 516)       /* the right column */
#define WST_BY(i)   (WC_Y + 116 + (i) * WST_PITCH + ((i) == 3 && WST_GFX_NOT_YET ? WST_GFX_DY : 0))   /* a block: its label; its control 30 under, 48 tall; its note 88 under
                                                       (the bottom row a little lower, under its GRAPHICS heading) */
#define WST_CY(i)   (WST_BY(i) + 30)
#define WST_CH      48
#define WST_KNOB_W  92
#define WST_SX0     (WST_LX + WST_KNOB_W / 2)
#define WST_SX1     (WST_LX + WST_LW - WST_KNOB_W / 2)
#define WST_SX(pct) (WST_SX0 + (pct) * (WST_SX1 - WST_SX0) / 100)
#define WST_ARW_W   56
#define WST_SEG_W   110
#define WST_SEG_DX  120
#define WST_BTN_W   230
enum { WST_L_BRIGHT, WST_L_VOLUME, WST_L_LIGHT, WST_L_VIG };
enum { WST_R_FEED, WST_R_UPDATES, WST_R_LINK, WST_R_GFX };
/* GRAPHICS, NOT YET (p4-perf 0.4.3, Strato 11-22: "if we dont make use of it this release then yes we should gray it out, not
 * hide it. make it clear that its a setting just not available right now"): the QUALITY row shown in the right column, dim
 * (by brightness, never by colour alone), its level HIGH, NOT AVAILABLE YET under it - and nothing to tap (the page's tap
 * answers nothing on that row). The big screens' own GRAPHICS levels turn this off when they land. */
#define WST_GFX_NOT_YET 1
#ifdef TANK_TAB5
#define WST_GFX_DY  36                 /* (the GRAPHICS row's drop: room for its heading - the Tab5's glass is 80 px shorter) */
#else
#define WST_GFX_DY  40
#endif
enum { WST_HIT_TO_TANK = 110, WST_HIT_TO_KEEPER, WST_HIT_TO_SHOP, WST_HIT_TO_TANKS, WST_HIT_TO_VISITORS };
static int g_wst_bright = -1, g_wst_volume = -1;     /* what the page last drew (the kept page's key leaves them out: its boxes) */
static int wst_slider_at(float x, int lo, int step) {
    const float f = (x - WST_SX0) / (float)(WST_SX1 - WST_SX0);
    const int pct = (int)lroundf(f * 100 / step) * step;
    return pct < lo ? lo : pct > 100 ? 100 : pct;
}
static void wst_slider(ctx_t *c, int i, int lo, int pct) {       /* the strip: the rail lit up to the knob, the knob with its percent */
    const int y = WST_CY(i), cy = y + WST_CH / 2;
    pct = pct < lo ? lo : pct > 100 ? 100 : pct;
    const int kx = WST_SX(pct);
    rect_fill(c, WST_LX - 2, y - 2, WST_LW + 4, WST_CH + 4, W_PANE_BG);
    rect_fill(c, WST_LX + 4, cy - 4, kx - WST_LX - 4, 8, MSP_TEAL);
    rect_fill(c, kx, cy - 4, WST_LX + WST_LW - 4 - kx, 8, W_BTN_FILL);
    rect_edge(c, kx, cy - 4, WST_LX + WST_LW - 4 - kx, 8, MSP_DIM);
    char txt[8]; snprintf(txt, sizeof txt, pct ? "%d%%" : "OFF", pct);
    const bool off = pct == 0;
    button(c, kx - WST_KNOB_W / 2, y, WST_KNOB_W, WST_CH, off ? W_BTN_FILL : MSP_TEAL, MSP_TEAL, "", 3);
    draw_text(c, kx - text_w(txt, 3) / 2, y + (WST_CH - 21) / 2, 3, off ? MSP_TEAL : MSP_INK, txt);
}
static void wst_sliders(uint16_t *fb, int stride, int bright_pct, int volume) {   /* the moving boxes, by the same lines whole or kept */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    wst_slider(&c, WST_L_BRIGHT, SET_BRT_MIN, bright_pct);
    wst_slider(&c, WST_L_VOLUME, 0, volume);
    g_wk_n = 0;
    for (int i = WST_L_BRIGHT; i <= WST_L_VOLUME; i++) { short *q = g_wk_box + g_wk_n * 4; q[0] = (short)(WST_LX - 2); q[1] = (short)(WST_CY(i) - 2); q[2] = (short)(WST_LX + WST_LW + 1); q[3] = (short)(WST_CY(i) + WST_CH + 1); g_wk_n++; }
}
static void wst_seg(ctx_t *c, int x, int y, int w, const char *lab, bool on) {
    if (on) { button(c, x, y, w, WST_CH, MSP_TEAL, MSP_TEAL, "", 3); draw_text(c, x + (w - text_w(lab, 3)) / 2, y + (WST_CH - 21) / 2, 3, MSP_INK, lab); }
    else button(c, x, y, w, WST_CH, W_BTN_FILL, MSP_DIM, lab, 3);
}
static void wst_arrow(ctx_t *c, int x, int y, bool right, bool live) {
    const uint32_t rgb = live ? MSP_TEAL : 0x2c4a52;
    button(c, x, y, WST_ARW_W, WST_CH, W_BTN_FILL, rgb, "", 2);
    const int cx = x + WST_ARW_W / 2, cy = y + WST_CH / 2;
    for (int i = 0; i < 9; i++) { const int px = right ? cx - 4 + i : cx + 4 - i, hh = 8 - i; rect_fill(c, px, cy - hh, 1, 2 * hh + 1, rgb); }
}
static uint32_t wst_key(const tank_t *t) {
    uint32_t h = 2166136261u;
    h = WK_MIX(h, 0x5e); h = WK_MIX(h, tank_light_choice(t)); h = WK_MIX(h, t->autofeed_off); h = WK_MIX(h, render_vignette()); h = WK_MIX(h, collection_count() > 0);
    return h;
}
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    if (g_big_pocket || s_gfxp) { g_wk[0].ok = g_wk[1].ok = false; g_wk_n = -1; render_settings_small(t, fb, stride, bright_pct, volume); return; }
    const uint32_t key = wst_key(t);
    if (!g_ms_whole_dbg) {
        if (g_wk_frame != g_rframe && g_wk_frame + 1 != g_rframe) g_wk_run0 = g_rframe;
        g_wk_frame = g_rframe;
    }
    wk_t *kp = g_wk[0].fb == fb ? &g_wk[0] : g_wk[1].fb == fb ? &g_wk[1] : g_wk[0].frame <= g_wk[1].frame ? &g_wk[0] : &g_wk[1];
    const bool keep = !g_ms_whole_dbg && kp->fb == fb && kp->ok && kp->key == key && kp->other == g_ctx_other && stride == TANK_W
                      && kp->frame < g_rframe && kp->frame >= g_wk_run0;
    g_ms_in = true; g_dirty_hold = true; g_wk_n = -1;
    g_wst_bright = bright_pct; g_wst_volume = volume;
    if (keep) { wst_sliders(fb, stride, bright_pct, volume); kp->frame = g_rframe; g_wk_kept_n++; g_ms_in = false; return; }
    g_wk_whole_n++;
    ctx_t c = ctx_full(fb, stride, 1.0f);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    w_rail(&c, WR_SETTINGS, collection_count() > 0);
    w_pane(&c, WC_X, WC_W, 0x2c4a52);
    draw_text(&c, WC_X + (WC_W - text_w("SETTINGS", 4)) / 2, WC_Y + 20, 4, 0xffffff, "SETTINGS");
    for (int x = WC_X + 24; x < WC_X + WC_W - 24; x++) px_blend(&c, x, WC_Y + 64, MSP_DIM, 200);
    draw_text(&c, WST_LX, WC_Y + 84, 2, 0x5f8a92, "SCREEN AND SOUND");
    draw_text(&c, WST_RX, WC_Y + 84, 2, 0x5f8a92, "TANK");
    /* the left column */
    draw_text(&c, WST_LX, WST_BY(WST_L_BRIGHT), 3, MSP_TEAL, "BRIGHTNESS");
    draw_text(&c, WST_LX, WST_BY(WST_L_VOLUME), 3, MSP_TEAL, "VOLUME");
    draw_text(&c, WST_LX, WST_BY(WST_L_VOLUME) + 88, 2, 0x5f8a92, "FISH ARE QUIET AT NIGHT");
    {   const int ch = tank_light_choice(t), y = WST_CY(WST_L_LIGHT), bx = WST_LX + WST_ARW_W + 8, bw = WST_LW - 2 * (WST_ARW_W + 8);
        draw_text(&c, WST_LX, WST_BY(WST_L_LIGHT), 3, MSP_TEAL, "LIGHTS OUT");
        wst_arrow(&c, WST_LX, y, false, ch > 0);
        button(&c, bx, y, bw, WST_CH, MSP_TEAL, MSP_TEAL, "", 3);
        draw_text(&c, bx + (bw - text_w(SET_LIGHT[ch], 3)) / 2, y + (WST_CH - 21) / 2, 3, MSP_INK, SET_LIGHT[ch]);
        wst_arrow(&c, WST_LX + WST_LW - WST_ARW_W, y, true, ch < LIGHT_IDLE_N);
        draw_text(&c, WST_LX, WST_BY(WST_L_LIGHT) + 88, 2, 0x5f8a92, ch ? "THE LIGHT GOES OUT WHEN LEFT ALONE" : "DOUBLE-TAP THE GLASS FOR THE LIGHT");
    }
#if WST_GFX_NOT_YET
    {   /* GRAPHICS (Strato 11-22: the vignette "in the graphics menu"): the bottom row under its own heading - VIGNETTE (live, the
           pocket tanks' saved setting) and QUALITY (dimmed, NOT YET) */
        const int hy = WST_BY(WST_L_VIG) - 28;
        for (int x = WC_X + 24; x < WC_X + WC_W - 24; x++) px_blend(&c, x, hy - 8, MSP_DIM, 120);
        draw_text(&c, WST_LX, hy, 2, 0x5f8a92, "GRAPHICS");
    }
#endif
    draw_text(&c, WST_LX, WST_BY(WST_L_VIG), 3, MSP_TEAL, "VIGNETTE");
    wst_seg(&c, WST_LX, WST_CY(WST_L_VIG), WST_SEG_W, "ON", render_vignette());
    wst_seg(&c, WST_LX + WST_SEG_DX, WST_CY(WST_L_VIG), WST_SEG_W, "OFF", !render_vignette());
    draw_text(&c, WST_LX + 2 * WST_SEG_DX + 8, WST_CY(WST_L_VIG) + (WST_CH - 14) / 2, 2, 0x5f8a92, "DARKER CORNERS");
    /* the right column */
    draw_text(&c, WST_RX, WST_BY(WST_R_FEED), 3, MSP_TEAL, "AUTO FEED");
    wst_seg(&c, WST_RX, WST_CY(WST_R_FEED), WST_SEG_W, "ON", !t->autofeed_off);
    wst_seg(&c, WST_RX + WST_SEG_DX, WST_CY(WST_R_FEED), WST_SEG_W, "OFF", t->autofeed_off);
    draw_text(&c, WST_RX, WST_BY(WST_R_FEED) + 88, 2, 0x5f8a92, t->autofeed_off ? "EVERY MEAL IS YOURS TO GIVE" : "A PELLET DROPS FOR A HUNGRY FISH");
    draw_text(&c, WST_RX, WST_BY(WST_R_UPDATES), 3, MSP_TEAL, "UPDATES");
    button(&c, WST_RX, WST_CY(WST_R_UPDATES), WST_BTN_W, WST_CH, W_BTN_FILL, MSP_TEAL, "", 3);
    blit_icon(&c, WST_RX + 10, WST_CY(WST_R_UPDATES) + (WST_CH - icon_settings_updates.h) / 2, &icon_settings_updates, 255);
    draw_text(&c, WST_RX + 60, WST_CY(WST_R_UPDATES) + (WST_CH - 21) / 2, 3, 0xffffff, "CHECK");
    draw_text(&c, WST_RX, WST_BY(WST_R_LINK), 3, MSP_TEAL, "TANK LINK");
    button(&c, WST_RX, WST_CY(WST_R_LINK), WST_BTN_W, WST_CH, W_BTN_FILL, MSP_TEAL, "", 3);
    blit_icon(&c, WST_RX + 10, WST_CY(WST_R_LINK) + (WST_CH - icon_settings_link.h) / 2, &icon_settings_link, 255);
    draw_text(&c, WST_RX + 60, WST_CY(WST_R_LINK) + (WST_CH - 21) / 2, 3, 0xffffff, "LINK");
    draw_text(&c, WST_RX, WST_BY(WST_R_LINK) + 88, 2, 0x5f8a92, "TWO TANKS SIDE BY SIDE");
#if WST_GFX_NOT_YET
    {   static const char *const LV[3] = { "LOW", "MEDIUM", "HIGH" };
        const int y = WST_CY(WST_R_GFX), sw = 140, dx = 150;
        draw_text(&c, WST_RX, WST_BY(WST_R_GFX), 3, 0x3e646c, "QUALITY");
        for (int k = 0; k < 3; k++) {                 /* the level kept: HIGH, a dim fill - lit as nothing else on the page is lit */
            const bool on = k == 2; const int x = WST_RX + k * dx;
            button(&c, x, y, sw, WST_CH, on ? 0x223a40 : W_BTN_FILL, 0x2c4a52, "", 3);
            draw_text(&c, x + (sw - text_w(LV[k], 3)) / 2, y + (WST_CH - 21) / 2, 3, on ? 0x5f8a92 : 0x3e646c, LV[k]);
        }
        draw_text(&c, WST_RX, WST_BY(WST_R_GFX) + 88, 2, 0x5f8a92, "NOT AVAILABLE YET");
    }
#endif
    {   char ver[64]; snprintf(ver, sizeof ver, "V%s %s  BUILD %s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string());
        draw_text_8px(&c, WST_LX, WC_Y + WC_H - 16, MSP_DIM, ver); }
    wst_sliders(fb, stride, bright_pct, volume);
    g_wk_n = -1;                                                       /* (drawn whole: turned whole) */
    if (!g_ms_whole_dbg) { kp->fb = fb; kp->key = key; kp->ok = true; kp->frame = g_rframe; kp->other = g_ctx_other; }
    g_ms_in = false;
}
/* the hit test (FRAME px): SET_TAP_* as the pocket page's, the LIGHTS OUT row's SET_HIT_LIGHT_PREV / _NEXT, the rail's
   WST_HIT_TO_* (render_settings_touch turns those into SET_TAP_TO_* / SET_TAP_TANKS). A control's band: 14 above it to
   20 under (fingers report low); across, a segment or an arrow takes half the gap to its neighbour */
int render_settings_tap(float x, float y, int *value) {
    if (g_big_pocket || s_gfxp) return render_settings_tap_small(x, y, value);
    *value = 0;
    const int rh = w_rail_hit(x, y);
    if (rh == WR_N) return WST_HIT_TO_TANK;
    if (rh == WR_KEEPER) return WST_HIT_TO_KEEPER;
    if (rh == WR_UPGRADES) return WST_HIT_TO_SHOP;
    if (rh == WR_TANKS) return WST_HIT_TO_TANKS;
    if (rh == WR_VISITORS) return collection_count() > 0 ? WST_HIT_TO_VISITORS : SET_TAP_NONE;
    if (rh >= 0) return SET_TAP_NONE;
    const bool left = x < WST_RX - 24;
    int row = -1;
    for (int i = 0; i < 4; i++) if (y >= WST_CY(i) - 14 && y < WST_CY(i) + WST_CH + 20) row = i;
    if (row < 0) return SET_TAP_NONE;
    if (left) {
        if (x < WST_LX - 16 || x >= WST_LX + WST_LW + 16) return SET_TAP_NONE;
        if (row == WST_L_BRIGHT) { *value = wst_slider_at(x, SET_BRT_MIN, SET_BRT_STEP); return SET_TAP_BRIGHT; }
        if (row == WST_L_VOLUME) { *value = wst_slider_at(x, 0, SET_VOL_STEP); return SET_TAP_VOLUME; }
        if (row == WST_L_LIGHT) return x < WST_LX + WST_LW / 2 ? SET_HIT_LIGHT_PREV : SET_HIT_LIGHT_NEXT;
        if (x >= WST_LX + WST_SEG_DX + WST_SEG_W + 12) return SET_TAP_NONE;
        *value = x < WST_LX + WST_SEG_W + (WST_SEG_DX - WST_SEG_W) / 2; return SET_TAP_VIG;
    }
    if (x < WST_RX - 16) return SET_TAP_NONE;
    if (row == WST_R_FEED) { if (x >= WST_RX + WST_SEG_DX + WST_SEG_W + 12) return SET_TAP_NONE; *value = x < WST_RX + WST_SEG_W + (WST_SEG_DX - WST_SEG_W) / 2; return SET_TAP_FEED; }
    if (x >= WST_RX + WST_BTN_W + 16) return SET_TAP_NONE;
    return row == WST_R_UPDATES ? SET_TAP_UPDATES : row == WST_R_LINK ? SET_TAP_LINK : SET_TAP_NONE;
}
int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    if (g_big_pocket || s_gfxp) return render_settings_touch_small(t, x, y, down, value);
    int r = SET_TAP_NONE; *value = 0;
    const bool slid = s_set_hit == SET_TAP_BRIGHT || s_set_hit == SET_TAP_VOLUME;
    if (down && !s_set_down) {                                  /* press */
        s_set_px = x; s_set_py = y;
        s_set_hit = set_updback_spent(t, x, y) ? SET_TAP_NONE : render_settings_tap(x, y, &s_set_hv);   /* (where the UPDATES page's BACK just was: spent) */
        if (s_set_hit == SET_TAP_BRIGHT || s_set_hit == SET_TAP_VOLUME) { r = s_set_hit == SET_TAP_BRIGHT ? SET_TAP_BRIGHT_LIVE : SET_TAP_VOLUME_LIVE; *value = s_set_hv; }
    } else if (down && slid) {                                  /* a slider under a finger: the knob follows it, however far up or down the finger wanders */
        const bool brt = s_set_hit == SET_TAP_BRIGHT;
        const int v = brt ? wst_slider_at(x, SET_BRT_MIN, SET_BRT_STEP) : wst_slider_at(x, 0, SET_VOL_STEP);
        if (v != s_set_hv) { s_set_hv = v; r = brt ? SET_TAP_BRIGHT_LIVE : SET_TAP_VOLUME_LIVE; *value = v; }
    } else if (!down && s_set_down && slid) {                   /* let go: the last step it showed is the setting */
        r = s_set_hit; *value = s_set_hv;
    } else if (!down && s_set_down) {                           /* release: a tap, if it stayed on what it pressed */
        int v = 0; const int h = render_settings_tap(x, y, &v);
        const float dx = x - s_set_px, dy = y - s_set_py;
        if (h && h == s_set_hit && dx * dx + dy * dy < 24 * 24) {
            if (h == SET_HIT_LIGHT_PREV || h == SET_HIT_LIGHT_NEXT) {
                const int was = tank_light_choice(t), now = was + (h == SET_HIT_LIGHT_NEXT ? 1 : -1);
                if (now >= 0 && now <= LIGHT_IDLE_N) {
                    tank_light_choice_set(t, now); tank_emit(TEV_WHEEL_TICK, -1); progression_settings_changed();
                    if ((was == 0) != (now == 0)) { r = SET_TAP_LIGHT; *value = now != 0; }
                    else { r = SET_TAP_IDLE; *value = t->light_idle_s; }
                }
            } else if (h == SET_TAP_FEED) {
                if ((v == 0) != t->autofeed_off) { t->autofeed_off = v == 0; progression_settings_changed(); }
                r = SET_TAP_FEED; *value = v;
            } else if (h == SET_TAP_VIG) {                      /* (the pocket tanks' GRAPHICS page's row: the setting now, its bake owed) */
                const bool changed = (v != 0) != render_vignette(); render_set_vignette(v != 0);
                if (changed) render_gfx_owe(t);
                r = SET_TAP_VIG; *value = v;
            } else if (h == SET_TAP_UPDATES || h == SET_TAP_LINK) r = h;
            else if (h == WST_HIT_TO_TANK) r = SET_TAP_TO_TANK;
            else if (h == WST_HIT_TO_KEEPER) r = SET_TAP_TO_KEEPER;
            else if (h == WST_HIT_TO_SHOP) r = SET_TAP_TO_SHOP;
            else if (h == WST_HIT_TO_VISITORS) r = SET_TAP_TO_VISITORS;
            else if (h == WST_HIT_TO_TANKS) r = SET_TAP_TO_TANKS;
        }
    }
    s_set_down = down;
    return r;
}
/* (the tests, the shots) where a control is, frame px: a slider at a percent, or a button */
void render_big_settings_where(int what, int pct, int *x, int *y) {
    switch (what) {
    case BIGSET_BRIGHT: *x = WST_SX(pct); *y = WST_CY(WST_L_BRIGHT) + WST_CH / 2; break;
    case BIGSET_VOLUME: *x = WST_SX(pct); *y = WST_CY(WST_L_VOLUME) + WST_CH / 2; break;
    case BIGSET_LIGHT_PREV: *x = WST_LX + WST_ARW_W / 2; *y = WST_CY(WST_L_LIGHT) + WST_CH / 2; break;
    case BIGSET_LIGHT_NEXT: *x = WST_LX + WST_LW - WST_ARW_W / 2; *y = WST_CY(WST_L_LIGHT) + WST_CH / 2; break;
    case BIGSET_VIG_ON: *x = WST_LX + WST_SEG_W / 2; *y = WST_CY(WST_L_VIG) + WST_CH / 2; break;
    case BIGSET_VIG_OFF: *x = WST_LX + WST_SEG_DX + WST_SEG_W / 2; *y = WST_CY(WST_L_VIG) + WST_CH / 2; break;
    case BIGSET_FEED_ON: *x = WST_RX + WST_SEG_W / 2; *y = WST_CY(WST_R_FEED) + WST_CH / 2; break;
    case BIGSET_FEED_OFF: *x = WST_RX + WST_SEG_DX + WST_SEG_W / 2; *y = WST_CY(WST_R_FEED) + WST_CH / 2; break;
    case BIGSET_UPDATES: *x = WST_RX + WST_BTN_W / 2; *y = WST_CY(WST_R_UPDATES) + WST_CH / 2; break;
    default: *x = WST_RX + WST_BTN_W / 2; *y = WST_CY(WST_R_LINK) + WST_CH / 2; break;
    }
}

/* ---- MY TANKS ----
 * The pocket page's two shelves of four, at their own size, on a wall that fills the list pane; the picked tank's card
 * is the detail pane, and stays open while the shelves beside it stay live (a tap on another tank picks that one; the
 * pocket card's "tap outside closes it" has nothing to close here). The picked tank wears a frame on the shelf. The
 * foot keeps THE LIBRARY, the TOUCH LOCK and the pager; the pocket foot's cog and X are the rail's SETTINGS and CLOSE.
 * A locked board's rail is dim but for CLOSE, and so are KEEPER / UPGRADES / VISITORS on a tank that has no such pages
 * (an experimental one). The page is still: main.c draws it once into its kept copy, as it does the pocket page.
 * THE LIBRARY, RENAME's keyboard, REMOVE and the lock's note are the pocket pages still, in the middle of the glass. */
#define WSF_GAP      60                                  /* a plank's foot to the next row's lid */
#define WSF_BLOCK    (2 * SH_SHELF_H + WSF_GAP)
#define WSF_TITLE_Y  (WC_Y + 22)
#define WSF_ROW1_Y   (WC_Y + 76 + (WC_H - 76 - 84 - WSF_BLOCK) / 2)
#define WSF_X0       (WL_X + (WL_W - SH_ROW_W) / 2)
#define WSF_FOOT_H   WST_CH
#define WSF_FOOT_Y   (WC_Y + WC_H - 24 - WSF_FOOT_H)
#define WSF_IC_W     64
#define WSF_LIB_X    (WL_X + 24)
#define WSF_LOCK_X   (WSF_LIB_X + WSF_IC_W + 12)
#define WSF_CANCEL_W 132
#define WSF_NEXT_X   (WL_X + WL_W - 24 - WST_ARW_W)
#define WSC_ART_Y    (WC_Y + 28)                         /* the card: the little tank at twice its size */
#define WSC_NAME_Y   (WC_Y + 206)
#define WSC_LINE_Y   (WC_Y + 248)
#define WSC_LINE_DY  22
#define WSC_DIV_Y    (WC_Y + 346)
#define WSC_TIME_Y   (WC_Y + 366)
#define WSC_NOTE_Y   (WC_Y + 428)
#define WSC_BTN_Y    (WC_Y + 472)
#define WSC_BTN_H    56
#define WSC_BTN_X0   (WD_X + 24)
#define WSC_SEG_X    (WD_X + 32 + 4 * 12 + 16)           /* after TIME */
#define WSC_SEG_W    (WD_X + WD_W - 32 - WSC_SEG_X)
#define WSC_REN_W    180                                 /* the tank on the glass: RENAME alone */
static const short WSC_BW[3] = { 164, 144, 92 };         /* SWAP IN, RENAME, MORE (12 between) */
static int  g_wsf_sel = -1;           /* the place (0..7 on this page) whose card the pane shows, or -1 */
static bool g_wsf_fish = true;        /* the tank on the glass has a keeper page, a shop, visitors */
static short g_wsf_prev_x;            /* the pager's left arrow, where it was last drawn */
void render_big_shelf_state(int sel, bool fish_glass) { g_wsf_sel = sel; g_wsf_fish = fish_glass; }
static int wsf_slot_x(int i) { return WSF_X0 + (i & 3) * (SH_TW + SH_GAP); }
static int wsf_slot_y(int i) { return WSF_ROW1_Y + (i >> 2) * (SH_SHELF_H + WSF_GAP); }
static void wsf_disc(ctx_t *c, int cx, int cy) {         /* the rest signs' dark disc */
    for (int dy = -9; dy <= 9; dy++) for (int dx = -9; dx <= 9; dx++) { int d = dx * dx + dy * dy;
        if (d <= 64) px_blend(c, cx + dx, cy + dy, 0x0b0f12, 220); else if (d <= 90) px_blend(c, cx + dx, cy + dy, 0x9fb4ba, 200); }
}
static void wsf_hint(ctx_t *c) {
    wsh_mid(c, WC_Y + WC_H / 2 - 20, 2, MSP_QUIET, "TAP A TANK ON THE SHELF");
    wsh_mid(c, WC_Y + WC_H / 2 + 4, 2, MSP_QUIET, "ITS CARD OPENS HERE");
}
void render_shelf(uint16_t *fb, int stride, const shelf_slot_t slot[8], int page, int pages) {
    if (g_big_pocket) { render_shelf_small(fb, stride, slot, page, pages); return; }
    ctx_t c = ctx_frame(fb, stride);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    const bool lock = g_sh_touch_lock, moving = g_sh_moving;
    w_rail(&c, WR_TANKS, collection_count() > 0);
    for (int i = 0; i < WR_N; i++)                         /* the rail's dead buttons, dim */
        if (i != WR_TANKS && (lock || (i != WR_SETTINGS && !g_wsf_fish))) button(&c, WR_X, WR_Y0 + i * WR_DY, WR_W, WR_H, W_BTN_FILL, MSP_DIM, WR_WORDS[i], 3);
    /* the wall and its shelves */
    w_pane(&c, WL_X, WL_W, 0x2c4a52);
    for (int y = WC_Y + 1; y < WC_Y + WC_H - 1; y++) rect_fill(&c, WL_X + 1, y, WL_W - 2, 1, sh_mix(SH_WALL_TOP, SH_WALL_BOT, y * 256 / TANK_H));
    const int MX = WL_X + WL_W / 2;
    if (moving) { char t[32]; snprintf(t, sizeof t, "MOVE %s", g_sh_move_name); draw_text(&c, MX - text_w(t, 3) / 2, WSF_TITLE_Y + 4, 3, 0xffffff, t); }
    else if (progression_bowl_has()) {                     /* THE TRAVEL BOWL: a fish waits in it */
        char l[24]; snprintf(l, sizeof l, "%s WAITS", progression_bowl_name());
        draw_text(&c, MX - text_w(l, 2) / 2, WSF_TITLE_Y - 4, 2, 0xffffff, l);
        draw_text(&c, MX - text_w("IN THE BOWL", 2) / 2, WSF_TITLE_Y + 16, 2, 0xffffff, "IN THE BOWL");
    } else draw_text(&c, MX - text_w("MY TANKS", 4) / 2, WSF_TITLE_Y, 4, 0xffffff, "MY TANKS");
    for (int row = 0; row < 2; row++) sh_plank(&c, WSF_X0 - SH_OVER, wsf_slot_y(row * 4) + SH_BOX_H, SH_ROW_W + 2 * SH_OVER);
    for (int i = 0; i < 8; i++) {
        const shelf_slot_t *s = &slot[i];
        int x = wsf_slot_x(i), y = wsf_slot_y(i);
        const int face = y + SH_BOX_H + SH_TOP;
        if (s->state == SHELF_EMPTY) {
            const uint32_t col = moving ? MSP_TEAL : 0x5d6e72;
            sh_dashed(&c, x, y, SH_TW, SH_BOX_H, col);
            const int cx = x + SH_TW / 2, cy = y + SH_BOX_H / 2;
            rect_fill(&c, cx - 9, cy - 1, 19, 3, moving ? MSP_TEAL : MSP_QUIET); rect_fill(&c, cx - 1, cy - 9, 3, 19, moving ? MSP_TEAL : MSP_QUIET);
            continue;
        }
        const bool held = moving && i == g_sh_move, now = s->state == SHELF_NOW;
        if (held) {                                        /* the tank in hand: lifted, framed, its place a shadow */
            for (int yy = 0; yy < 6; yy++) rect_fill(&c, x + 4, y + SH_BOX_H - 6 + yy, SH_TW - 8, 1, sh_shade(0x3a2a1e, 60 + yy * 4));
            y -= 10;
            rect_edge(&c, x - 3, y - 3, SH_TW + 6, SH_BOX_H + 6, MSP_TEAL); rect_edge(&c, x - 2, y - 2, SH_TW + 4, SH_BOX_H + 4, MSP_TEAL);
        }
        if (now)
            for (int yy = 0; yy < SH_TOP; yy++)
                for (int xx = -8; xx < SH_TW + 8; xx++) {
                    const int edge = xx < 0 ? -xx : xx >= SH_TW ? xx - SH_TW + 1 : 0;
                    px_blend(&c, x + xx, y + SH_BOX_H + yy, 0xfff0c8, 110 - edge * 12);
                }
        sh_tank(&c, x, y, SH_TW, SH_IH, s->thumb, !now);
        if (!moving && i == g_wsf_sel) {                   /* the tank whose card is open: a frame (a shape, not a hue) */
            rect_edge(&c, x - 4, y - 4, SH_TW + 8, SH_BOX_H + 8, 0xffffff); rect_edge(&c, x - 3, y - 3, SH_TW + 6, SH_BOX_H + 6, 0xffffff);
        }
        if (now) {
            const int w = text_w("NOW", 2) + 12, tx = x + (SH_TW - w) / 2, ty = y - 24;
            rect_fill(&c, tx, ty, w, 18, MSP_TEAL);
            draw_text(&c, tx + 6, ty + 2, 2, MSP_INK, "NOW");
        } else sh_rest_sign(&c, x + SH_TW - 13, y + SH_LID + 12, s->state);
        if (now && lock) { wsf_disc(&c, x + SH_TW - 13, y + SH_LID + 12); sh_padlock(&c, x + SH_TW - 13, y + SH_LID + 13, true, 0xe2edf0, 0x0b0f12); }
        if (s->experimental) { wsf_disc(&c, x + 13, y + SH_BOX_H - 15); sh_flask(&c, x + 10, y + SH_BOX_H - 19, 0xe2edf0); }
        sh_plate(&c, x + SH_TW / 2, face + 1, s->name);
    }
    /* the foot */
    if (moving) button(&c, WSF_LIB_X, WSF_FOOT_Y, WSF_CANCEL_W, WSF_FOOT_H, W_BTN_FILL, MSP_TEAL, "CANCEL", 2);
    else {
        button(&c, WSF_LIB_X, WSF_FOOT_Y, WSF_IC_W, WSF_FOOT_H, W_BTN_FILL, MSP_TEAL, "", 2);
        sh_ic_library(&c, WSF_LIB_X + WSF_IC_W / 2, WSF_FOOT_Y + WSF_FOOT_H / 2, MSP_TEAL, W_BTN_FILL);
        button(&c, WSF_LOCK_X, WSF_FOOT_Y, WSF_IC_W, WSF_FOOT_H, lock ? MSP_TEAL : W_BTN_FILL, MSP_TEAL, "", 2);
        sh_padlock(&c, WSF_LOCK_X + WSF_IC_W / 2, WSF_FOOT_Y + WSF_FOOT_H / 2 + 1, lock, lock ? MSP_INK : MSP_TEAL, lock ? MSP_TEAL : W_BTN_FILL);
    }
    {   char t[16]; snprintf(t, sizeof t, "%d OF %d", page, pages);
        const int tx = WSF_NEXT_X - 14 - text_w(t, 2);
        g_wsf_prev_x = (short)(tx - 14 - WST_ARW_W);
        wst_arrow(&c, g_wsf_prev_x, WSF_FOOT_Y, false, page > 1);
        draw_text(&c, tx, WSF_FOOT_Y + (WSF_FOOT_H - 14) / 2, 2, pages > 1 ? 0xffffff : MSP_QUIET, t);
        wst_arrow(&c, WSF_NEXT_X, WSF_FOOT_Y, true, page < pages);
    }
    /* the pane: a card is drawn over this (render_shelf_card) */
    w_pane(&c, WD_X, WD_W, MSP_TEAL);
    wsf_hint(&c);
}
int render_shelf_tap(float x, float y) {
    if (g_big_pocket) return render_shelf_tap_small(x, y);
    const int rh = w_rail_hit(x, y);
    if (rh >= 0) {
        const bool live = !g_sh_touch_lock && g_wsf_fish;
        if (rh == WR_N) return SHELF_TAP_CLOSE;
        if (rh == WR_SETTINGS) return SHELF_TAP_SETTINGS;      /* (a locked board: main.c says no) */
        if (rh == WR_KEEPER) return live ? SHELF_TAP_TO_KEEPER : SHELF_TAP_NONE;
        if (rh == WR_UPGRADES) return live ? SHELF_TAP_TO_SHOP : SHELF_TAP_NONE;
        if (rh == WR_VISITORS) return live && collection_count() > 0 ? SHELF_TAP_TO_VISITORS : SHELF_TAP_NONE;
        return SHELF_TAP_NONE;
    }
    if (x < WL_X || x >= WL_X + WL_W) return SHELF_TAP_NONE;
    if (y >= WSF_FOOT_Y - 12) {                                /* the foot, down to the glass's edge */
        if (x >= g_wsf_prev_x - 10 && x < g_wsf_prev_x + WST_ARW_W + 10) return SHELF_TAP_PREV;
        if (x >= WSF_NEXT_X - 10) return SHELF_TAP_NEXT;
        if (g_sh_moving) return x < WSF_LIB_X + WSF_CANCEL_W + 10 ? SHELF_TAP_LIBRARY : SHELF_TAP_NONE;   /* (CANCEL: the caller reads LIBRARY) */
        if (x < WSF_LIB_X + WSF_IC_W + 6) return SHELF_TAP_LIBRARY;
        if (x < WSF_LOCK_X + WSF_IC_W + 10) return SHELF_TAP_TOUCH_LOCK;
        return SHELF_TAP_NONE;
    }
    for (int i = 0; i < 8; i++) {
        const int sx = wsf_slot_x(i), sy = wsf_slot_y(i);
        if (x >= sx - 6 && x < sx + SH_TW + 6 && y >= sy - 10 && y < sy + SH_SHELF_H + 6) return SHELF_TAP_SLOT0 + i;
    }
    return SHELF_TAP_NONE;
}
static void wsf_tank2(ctx_t *c, int x, int y, const uint16_t *pic, bool dim) {     /* a little tank at twice its size (sh_tank's lines, sh_pic's dimming) */
    const int W = 2 * SH_TW, L = 2 * SH_LID, IH = 2 * SH_IH, B = 2 * SH_BASE;
    rect_fill(c, x, y, W, L, 0x1b1f22); rect_fill(c, x, y, W, 2, 0x4d575c);
    if (pic) { for (int j = 0; j < IH; j++) for (int i = 0; i < W - 8; i++) { uint16_t v = pic[(j / 2) * SH_PIC_W + i / 2];
                   if (dim) { const int r = (v >> 11) * 9 / 20, g = ((v >> 5) & 63) * 9 / 20, b = (v & 31) * 11 / 20 + 1; v = (uint16_t)(r << 11 | g << 5 | (b > 31 ? 31 : b)); }
                   px(c, x + 4 + i, y + L + j, v); } }
    else rect_fill(c, x + 4, y + L, W - 8, IH, 0x05080a);
    const uint32_t side = dim ? 0x4e6268 : 0x9cc2ca;
    rect_fill(c, x, y + L, 4, IH, side); rect_fill(c, x + W - 4, y + L, 4, IH, side);
    for (int j = 0; j < IH * 2 / 3; j++) for (int k = 0; k < 6; k++) px_blend(c, x + 16 + j / 3 + k, y + L + 4 + j, 0xffffff, dim ? 12 : 22);
    rect_fill(c, x, y + L + IH, W, B, 0x15181a); rect_fill(c, x, y + L + IH + B - 2, W, 2, 0x0a0c0d);
}
void render_shelf_card(uint16_t *fb, int stride, const shelf_slot_t *s, const char *kind, int shelf_days, int age_days) {
    if (g_big_pocket) { render_shelf_card_small(fb, stride, s, kind, shelf_days, age_days); return; }
    ctx_t c = ctx_frame(fb, stride);
    const int CX = WD_X + WD_W / 2;
    w_pane(&c, WD_X, WD_W, MSP_TEAL);
    wsf_tank2(&c, CX - SH_TW, WSC_ART_Y, s->thumb, false);
    char t[48];
    { char nm[24]; snprintf(nm, sizeof nm, "%s", s->name); for (char *p = nm; *p; p++) if (*p == '\n') *p = ' ';
      wsh_mid(&c, WSC_NAME_Y, 4, 0xffffff, nm); }
    int ty = WSC_LINE_Y;
    wsh_mid(&c, ty, 2, MSP_TEAL, kind); ty += WSC_LINE_DY;
    if (s->experimental) { const int w = 12 + text_w("EXPERIMENTAL", 2);
        sh_flask(&c, CX - w / 2, ty + 3, MSP_QUIET); draw_text(&c, CX - w / 2 + 12, ty, 2, MSP_QUIET, "EXPERIMENTAL"); ty += WSC_LINE_DY; }
    const bool glass = s->state == SHELF_NOW;
    g_shc_glass = glass;
    if (glass) snprintf(t, sizeof t, "ON THE GLASS"); else snprintf(t, sizeof t, "SHELVED %d DAY%s", shelf_days, shelf_days == 1 ? "" : "S");
    wsh_mid(&c, ty, 2, MSP_TEAL, t); ty += WSC_LINE_DY;
    if (g_sh_jelly) { snprintf(t, sizeof t, "%s THEME", g_sh_jelly); wsh_mid(&c, ty, 2, MSP_TEAL, t); }
    else if (age_days >= 0) { snprintf(t, sizeof t, "%d DAYS OLD", age_days); wsh_mid(&c, ty, 2, MSP_TEAL, t); }
    for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(&c, x, WSC_DIV_Y, MSP_DIM, 200);
    if (glass) {                                           /* in the TIME row's place: what the shelf does with it; RENAME */
        wsh_mid(&c, WSC_TIME_Y + 4, 2, MSP_QUIET, "SWAP IN ANOTHER TANK");
        wsh_mid(&c, WSC_TIME_Y + 28, 2, MSP_QUIET, "TO PUT THIS ONE AWAY");
        button(&c, CX - WSC_REN_W / 2, WSC_BTN_Y, WSC_REN_W, WSC_BTN_H, W_BTN_FILL, MSP_TEAL, "RENAME", 3);
        return;
    }
    draw_text(&c, WD_X + 32, WSC_TIME_Y + (WST_CH - 14) / 2, 2, MSP_TEAL, "TIME");
    const bool paused = s->state == SHELF_PAUSED;
    for (int k = 0; k < 2; k++) {
        const bool on = (k == 1) == paused;
        const char *lab = k ? "PAUSED" : "DORMANT";
        const int half = WSC_SEG_W / 2, bx = WSC_SEG_X + k * half, bw = k ? WSC_SEG_W - half : half;
        if (on) rect_fill(&c, bx, WSC_TIME_Y, bw, WST_CH, MSP_TEAL);
        else { rect_fill(&c, bx, WSC_TIME_Y, bw, WST_CH, W_BTN_FILL); rect_edge(&c, bx, WSC_TIME_Y, bw, WST_CH, MSP_TEAL); }
        draw_text(&c, bx + (bw - text_w(lab, 2)) / 2, WSC_TIME_Y + (WST_CH - 14) / 2, 2, on ? MSP_INK : MSP_QUIET, lab);
    }
    wsh_mid(&c, WSC_NOTE_Y, 2, MSP_QUIET, paused ? "WAITS AS YOU LEFT IT" : "LIVES ON, AS IF ASLEEP");
    int bx = WSC_BTN_X0;
    if (g_shc_swap >= 0) swap_button(&c, bx, WSC_BTN_Y, WSC_BW[0], WSC_BTN_H, g_shc_swap);
    else { button(&c, bx, WSC_BTN_Y, WSC_BW[0], WSC_BTN_H, MSP_TEAL, MSP_TEAL, "", 3);
           draw_text(&c, bx + (WSC_BW[0] - text_w("SWAP IN", 3)) / 2, WSC_BTN_Y + (WSC_BTN_H - 21) / 2, 3, MSP_INK, "SWAP IN"); }
    bx += WSC_BW[0] + 12;
    button(&c, bx, WSC_BTN_Y, WSC_BW[1], WSC_BTN_H, W_BTN_FILL, MSP_TEAL, "RENAME", 3); bx += WSC_BW[1] + 12;
    button(&c, bx, WSC_BTN_Y, WSC_BW[2], WSC_BTN_H, W_BTN_FILL, MSP_TEAL, "MORE", 3);
}
/* the card's places: OUTSIDE = not the pane (main.c gives that tap to the shelves) */
int render_shelf_card_tap(float x, float y) {
    if (g_big_pocket) return render_shelf_card_tap_small(x, y);
    if (x < WD_X || x >= WD_X + WD_W || y < WC_Y || y >= WC_Y + WC_H) return SHC_TAP_OUTSIDE;
    const bool brow = y >= WSC_BTN_Y - 14 && y < WSC_BTN_Y + WSC_BTN_H + 14;
    if (g_shc_glass) { const int bx = WD_X + (WD_W - WSC_REN_W) / 2; return brow && x >= bx - 20 && x < bx + WSC_REN_W + 20 ? SHC_TAP_RENAME : SHC_TAP_NONE; }
    if (y >= WSC_TIME_Y - 10 && y < WSC_TIME_Y + WST_CH + 10 && x >= WSC_SEG_X - 10) return x < WSC_SEG_X + WSC_SEG_W / 2 ? SHC_TAP_DORMANT : SHC_TAP_PAUSED;
    if (brow) {
        int bx = WSC_BTN_X0;
        if (x < bx + WSC_BW[0] + 6) return SHC_TAP_SWAP;
        bx += WSC_BW[0] + 12;
        if (x < bx + WSC_BW[1] + 6) return SHC_TAP_RENAME;
        return SHC_TAP_MORE;
    }
    return SHC_TAP_NONE;
}
/* (the tests, the shots) where a control is, frame px */
void render_big_shelf_where(int what, int *x, int *y) {
    if (what >= BIGSH_SLOT0 && what < BIGSH_SLOT0 + 8) { *x = wsf_slot_x(what - BIGSH_SLOT0) + SH_TW / 2; *y = wsf_slot_y(what - BIGSH_SLOT0) + SH_BOX_H / 2; return; }
    if (what >= BIGSH_RAIL0 && what <= BIGSH_RAIL0 + WR_N) { const int i = what - BIGSH_RAIL0; *x = WR_X + WR_W / 2; *y = i == WR_N ? WR_CLOSE_Y + WR_H / 2 : WR_Y0 + i * WR_DY + WR_H / 2; return; }
    *y = WSF_FOOT_Y + WSF_FOOT_H / 2;
    switch (what) {
    case BIGSH_LIB: *x = WSF_LIB_X + WSF_IC_W / 2; break;
    case BIGSH_LOCK: *x = WSF_LOCK_X + WSF_IC_W / 2; break;
    case BIGSH_PREV: *x = g_wsf_prev_x + WST_ARW_W / 2; break;
    case BIGSH_NEXT: *x = WSF_NEXT_X + WST_ARW_W / 2; break;
    case BIGSH_DORMANT: *x = WSC_SEG_X + WSC_SEG_W / 4; *y = WSC_TIME_Y + WST_CH / 2; break;
    case BIGSH_PAUSED: *x = WSC_SEG_X + WSC_SEG_W * 3 / 4; *y = WSC_TIME_Y + WST_CH / 2; break;
    case BIGSH_SWAP: *x = WSC_BTN_X0 + WSC_BW[0] / 2; *y = WSC_BTN_Y + WSC_BTN_H / 2; break;
    case BIGSH_RENAME: *x = WSC_BTN_X0 + WSC_BW[0] + 12 + WSC_BW[1] / 2; *y = WSC_BTN_Y + WSC_BTN_H / 2; break;
    case BIGSH_MORE: *x = WSC_BTN_X0 + WSC_BW[0] + WSC_BW[1] + 24 + WSC_BW[2] / 2; *y = WSC_BTN_Y + WSC_BTN_H / 2; break;
    default: *x = WD_X + WD_W / 2; *y = WSC_BTN_Y + WSC_BTN_H / 2; break;      /* BIGSH_GLASS_RENAME */
    }
}

/* ---- VISITORS ----
 * The collection's twelve a page, three across, in the list pane; the picked visitor's passport is the detail pane and
 * stays open (the pocket page's card over a dimmed grid has nothing to dim here). A page opens on its first visitor.
 * The pager is in the foot (the pocket page's arrows at the title's ends), wrapping as it does there.
 * THE WIDE PAGE KEPT: the portraits are its moving boxes. The rail's answers are COLL_TAP_TO_*; KEEPER is the pocket
 * page's CLOSE (back to the keeper page). */
#define WV_COLS    3
#define WV_ROWS    (COLL_PAGE / WV_COLS)
#define WV_CW      144
#ifdef TANK_TAB5
#define WV_CH      120
#else
#define WV_CH      136
#endif
#define WV_X0      (WL_X + (WL_W - WV_COLS * WV_CW) / 2)
#define WV_Y0      (WC_Y + 84)
#define WV_FOOT_Y  (WC_Y + WC_H - 24 - WST_CH)
#define WV_PREV_X  (WL_X + 24)
#define WV_NEXT_X  (WL_X + WL_W - 24 - WST_ARW_W)
#define WV_PORT_W  220                                   /* the passport's portrait */
#define WV_PORT_H  164
#define WV_PORT_Y  (WC_Y + 56)
#define WV_NAME_Y  (WC_Y + 234)
#define WV_FLD_Y   (WC_Y + 280)
#define WV_FLD_DY  52
#define WV_LBL     0x7fa3b5
_Static_assert(WK_MAX >= COLL_PAGE + 1, "the visitors' page: a box a portrait");
static int wv_sel(void) {                                /* the visitor whose passport shows: the picked one on this page, else the page's first */
    const int n = collection_count(), p0 = g_coll_page * COLL_PAGE;
    if (g_coll_sel >= p0 && g_coll_sel < p0 + COLL_PAGE && g_coll_sel < n) return g_coll_sel;
    return p0 < n ? p0 : -1;
}
typedef struct { char hlbl[40], home[24], form[24], met[24], here[24], visits[24], fry[24]; } wv_words_t;
static void wv_words(const coll_entry_t *e, wv_words_t *w) {
    home_fields(e->home, e->home_name, "HOME", w->hlbl, sizeof w->hlbl, w->home, sizeof w->home);
    snprintf(w->form, sizeof w->form, "%s %s", SHAPE_NAMES[e->shape % SHAPE_N], FINS_NAMES[e->fins % FINS_N]);
    fmt_met(w->met, sizeof w->met, e->met_unix); fmt_dur(w->here, sizeof w->here, e->here_s);
    snprintf(w->visits, sizeof w->visits, "%u VISIT%s", (unsigned)e->visits, e->visits == 1 ? "" : "S");
    if (e->fry) snprintf(w->fry, sizeof w->fry, "%u FRY HERE", (unsigned)e->fry); else w->fry[0] = 0;
}
static uint32_t wv_key(void) {
    uint32_t h = 2166136261u;
    const int n = collection_count(), sel = wv_sel();
    h = WK_MIX(h, 0x56); h = WK_MIX(h, g_coll_page); h = WK_MIX(h, sel); h = WK_MIX(h, n); h = WK_MIX(h, collection_pages());
    for (int j = 0; j < COLL_PAGE; j++) {
        const int i = g_coll_page * COLL_PAGE + j; if (i >= n) break;
        const coll_entry_t *e = collection_get(i);
        h = WK_MIX(h, e->uid); h = WK_MIX(h, e->color); h = WK_MIX(h, e->fin); h = WK_MIX(h, e->accent); h = WK_MIX(h, e->stage); h = WK_MIX(h, e->shape); h = WK_MIX(h, e->fins); h = WK_MIX(h, e->pattern);
        for (const char *q = e->name; *q; q++) h = WK_MIX(h, *q);
    }
    if (sel >= 0) { wv_words_t w; wv_words(collection_get(sel), &w);
        const char *all[7] = { w.hlbl, w.home, w.form, w.met, w.here, w.visits, w.fry };
        for (int k = 0; k < 7; k++) { for (const char *q = all[k]; *q; q++) h = WK_MIX(h, *q); h = WK_MIX(h, k); } }
    return h;
}
#define WK_BOX(x0_, y0_, x1_, y1_, bg_, draw_) do { const int bx0 = (x0_), by0 = (y0_), bx1 = (x1_), by1 = (y1_); \
        rect_fill(&c, bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1, (bg_)); g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -(1 << 20); draw_; g_bb_on = false; \
        struct { int x0, y0, x1, y1; } b_ = { g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1 }; \
        if (b_.x0 <= b_.x1 && (b_.x0 + PAGE_X < bx0 || b_.x1 + PAGE_X > bx1 || b_.y0 + PAGE_Y < by0 || b_.y1 + PAGE_Y > by1)) in = false; \
        if (g_wk_n < WK_MAX) { short *q = g_wk_box + g_wk_n * 4; q[0] = (short)bx0; q[1] = (short)by0; q[2] = (short)bx1; q[3] = (short)by1; g_wk_n++; } } while (0)
static bool wv_anim_all(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    bool in = true; g_wk_n = 0;
    const int n = collection_count(), sel = wv_sel();
    for (int j = 0; j < COLL_PAGE; j++) {
        const int i = g_coll_page * COLL_PAGE + j; if (i >= n) break;
        const int cx = WV_X0 + (j % WV_COLS) * WV_CW, cy = WV_Y0 + (j / WV_COLS) * WV_CH;
        fish_t f; entry_fish(&f, collection_get(i));
        WK_BOX(cx + 8, cy + 6, cx + WV_CW - 9, cy + WV_CH - 36, COLL_BOX,
               render_fish_portrait(fb, stride, cx + WV_CW / 2.0f - PAGE_X, cy + (WV_CH - 30) / 2.0f + 2 - PAGE_Y, 1.2f, &f, WV_CW - 44, t->clock + j * 0.7f));
    }
    if (sel >= 0) {
        const int px0 = WD_X + (WD_W - WV_PORT_W) / 2;
        fish_t f; entry_fish(&f, collection_get(sel));
        WK_BOX(px0 + 1, WV_PORT_Y + 1, px0 + WV_PORT_W - 2, WV_PORT_Y + WV_PORT_H - 2, COLL_BOX,
               render_fish_portrait(fb, stride, px0 + WV_PORT_W / 2.0f - PAGE_X, WV_PORT_Y + WV_PORT_H / 2.0f + 4 - PAGE_Y, 1.9f, &f, WV_PORT_W - 50, t->clock));
    }
    return in;
}
#undef WK_BOX
/* THE NAVY PAGE (0.4.3, Strato 11-22 "yes navy"): the pocket collection page's look (collection-navy-043) on the wide page -
   its own defines (render.c COLL_NAVY and the rest), not a palette of this page's: navy panes, black places with gold
   edges, the title gold, the count cream, gold pips and arrows */
static void wv_pane(ctx_t *c, int x, int w) { rect_fill(c, x, WC_Y, w, WC_H, COLL_NAVY); rect_edge(c, x, WC_Y, w, WC_H, COLL_BOX_EDGE); }
static void wv_arrow(ctx_t *c, int x, int y, bool right) {
    rect_fill(c, x, y, WST_ARW_W, WST_CH, COLL_NAVY_DK); rect_edge(c, x, y, WST_ARW_W, WST_CH, COLL_GOLD);
    const int cx = x + WST_ARW_W / 2, cy = y + WST_CH / 2;
    for (int i = 0; i < 9; i++) { const int px = right ? cx - 4 + i : cx + 4 - i, hh = 8 - i; rect_fill(c, px, cy - hh, 1, 2 * hh + 1, COLL_GOLD); }
}
static void wv_field(ctx_t *c, int x, int y, const char *lbl, const char *val) {
    draw_text(c, x, y, 2, WV_LBL, lbl); draw_text(c, x, y + 20, 2, 0xffffff, val);
}
void render_collection(const tank_t *t, uint16_t *fb, int stride) {
    if (g_big_pocket) { g_wk[0].ok = g_wk[1].ok = false; g_wk_n = -1; render_collection_small(t, fb, stride); return; }
    const int n = collection_count(), np = collection_pages();
    if (g_coll_page >= np) g_coll_page = np - 1;
    const int sel = wv_sel();
    const uint32_t key = wv_key();
    if (!g_ms_whole_dbg) {
        if (g_wk_frame != g_rframe && g_wk_frame + 1 != g_rframe) g_wk_run0 = g_rframe;
        g_wk_frame = g_rframe;
    }
    wk_t *kp = g_wk[0].fb == fb ? &g_wk[0] : g_wk[1].fb == fb ? &g_wk[1] : g_wk[0].frame <= g_wk[1].frame ? &g_wk[0] : &g_wk[1];
    const bool keep = !g_ms_whole_dbg && kp->fb == fb && kp->ok && kp->key == key && kp->other == g_ctx_other && stride == TANK_W
                      && kp->frame < g_rframe && kp->frame >= g_wk_run0;
    g_ms_in = true; g_dirty_hold = true; g_wk_n = -1;
    if (keep && wv_anim_all(t, fb, stride)) { kp->frame = g_rframe; g_wk_kept_n++; g_ms_in = false; return; }
    g_wk_whole_n++; g_wk_n = -1;
    ctx_t c = ctx_full(fb, stride, 1.0f);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    w_rail(&c, WR_VISITORS, true);
    /* the grid */
    wv_pane(&c, WL_X, WL_W);
    char line[48];
    draw_text(&c, WL_X + (WL_W - text_w("VISITOR COLLECTION", 3)) / 2, WC_Y + 18, 3, COLL_GOLD, "VISITOR COLLECTION");
    snprintf(line, sizeof line, "%d OF %d COLLECTED", n, COLL_MAX);
    draw_text(&c, WL_X + (WL_W - text_w(line, 2)) / 2, WC_Y + 50, 2, COLL_CREAM, line);
    for (int j = 0; j < COLL_PAGE; j++) {
        const int i = g_coll_page * COLL_PAGE + j;
        const int cx = WV_X0 + (j % WV_COLS) * WV_CW, cy = WV_Y0 + (j / WV_COLS) * WV_CH;
        if (i >= n) { rect_edge(&c, cx + 6, cy + 4, WV_CW - 12, WV_CH - 8, COLL_EMPTY); continue; }   /* an empty place to fill: its outline */
        rect_fill(&c, cx + 6, cy + 4, WV_CW - 12, WV_CH - 8, COLL_BOX);
        if (i == sel) { rect_edge(&c, cx + 6, cy + 4, WV_CW - 12, WV_CH - 8, 0xffffff); rect_edge(&c, cx + 7, cy + 5, WV_CW - 14, WV_CH - 10, 0xffffff); }   /* picked: two white lines (a shape, not a colour) */
        else rect_edge(&c, cx + 6, cy + 4, WV_CW - 12, WV_CH - 8, COLL_BOX_EDGE);
        const coll_entry_t *e = collection_get(i);
        draw_text(&c, cx + (WV_CW - text_w(e->name, 2)) / 2, cy + WV_CH - 28, 2, 0xffffff, e->name);
    }
    if (np > 1) {
        wv_arrow(&c, WV_PREV_X, WV_FOOT_Y, false); wv_arrow(&c, WV_NEXT_X, WV_FOOT_Y, true);
        snprintf(line, sizeof line, "PAGE %d OF %d", g_coll_page + 1, np);
        draw_text(&c, WL_X + (WL_W - text_w(line, 2)) / 2, WV_FOOT_Y + 8, 2, COLL_CREAM, line);
        const int bx0 = WL_X + (WL_W - (np * COLL_PIP - 4)) / 2;                    /* a pip a page: the page's filled */
        for (int p = 0; p < np; p++) { const int bx = bx0 + p * COLL_PIP, by = WV_FOOT_Y + 32;
            if (p == g_coll_page) rect_fill(&c, bx - 1, by - 1, 8, 8, COLL_GOLD); else rect_edge(&c, bx, by, 6, 6, COLL_PIP_OFF); }
    }
    /* the passport */
    wv_pane(&c, WD_X, WD_W);
    if (sel >= 0) {
        const coll_entry_t *e = collection_get(sel); wv_words_t w; wv_words(e, &w);
        static const char *const STAGE_W[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
        rect_fill(&c, WD_X + 1, WC_Y + 1, WD_W - 2, 38, COLL_NAVY_DK);
        wsh_mid(&c, WC_Y + 10, 3, COLL_GOLD, "POCKET TANK PASSPORT");
        const int px0 = WD_X + (WD_W - WV_PORT_W) / 2;
        rect_fill(&c, px0, WV_PORT_Y, WV_PORT_W, WV_PORT_H, COLL_BOX); rect_edge(&c, px0, WV_PORT_Y, WV_PORT_W, WV_PORT_H, COLL_BOX_EDGE);
        wsh_mid(&c, WV_NAME_Y, 4, 0xffffff, e->name);
        const int LX = WD_X + 32, RX = WD_X + 252;
        wv_field(&c, LX, WV_FLD_Y, w.hlbl, w.home);
        wv_field(&c, LX, WV_FLD_Y + WV_FLD_DY, "FORM", w.form);
        wv_field(&c, LX, WV_FLD_Y + 2 * WV_FLD_DY, "STAGE", STAGE_W[e->stage & 3]);
        wv_field(&c, RX, WV_FLD_Y + 2 * WV_FLD_DY, "FIRST MET", w.met);
        wv_field(&c, LX, WV_FLD_Y + 3 * WV_FLD_DY, "VISITS", w.visits);
        wv_field(&c, RX, WV_FLD_Y + 3 * WV_FLD_DY, "IN YOUR TANK", w.here);
        if (w.fry[0]) wv_field(&c, LX, WV_FLD_Y + 4 * WV_FLD_DY, "FAMILY", w.fry);
        { const char *st = "COLLECTED"; const int sw = text_w(st, 2) + 16, sx0 = RX, sy0 = WV_FLD_Y + 4 * WV_FLD_DY + 4;   /* the stamp, as on its card */
          rect_edge(&c, sx0, sy0, sw, 30, COLL_GOLD); rect_edge(&c, sx0 + 2, sy0 + 2, sw - 4, 26, COLL_GOLD);
          draw_text(&c, sx0 + 8, sy0 + 8, 2, COLL_GOLD, st); }
        char m1[64]; const int cols = (WD_W - 48) / 12;
        snprintf(m1, sizeof m1, "P<FISH<<%s<<%08X", e->name, (unsigned)e->uid);
        for (int j = (int)strlen(m1); j < cols; j++) m1[j] = '<';
        m1[cols] = 0;
        draw_text(&c, WD_X + 24, WC_Y + WC_H - 30, 2, 0xc8d6de, m1);
    } else {
        wsh_mid(&c, WC_Y + WC_H / 2 - 20, 2, COLL_CREAM, "NO VISITORS COLLECTED YET");
    }
    {   const bool in = wv_anim_all(t, fb, stride);
        g_wk_n = -1;
        if (!g_ms_whole_dbg) { kp->fb = fb; kp->key = key; kp->ok = in; kp->frame = g_rframe; kp->other = g_ctx_other; }
    }
    g_ms_in = false;
}
int render_collection_tap(const tank_t *t, float x, float y) {
    if (g_big_pocket) return render_collection_tap_small(t, x, y);
    const int rh = w_rail_hit(x, y), np = collection_pages(), n = collection_count();
    if (rh == WR_N) return COLL_TAP_TO_TANK;
    if (rh == WR_KEEPER) return COLL_TAP_CLOSE;
    if (rh == WR_UPGRADES) return COLL_TAP_TO_SHOP;
    if (rh == WR_TANKS) return COLL_TAP_TO_TANKS;
    if (rh == WR_SETTINGS) return COLL_TAP_TO_SETTINGS;
    if (rh >= 0) return COLL_TAP_KEPT;
    if (x < WL_X || x >= WL_X + WL_W) return COLL_TAP_KEPT;      /* (the passport: nothing to tap) */
    if (np > 1 && y >= WV_FOOT_Y - 12) {
        if (x < WV_PREV_X + WST_ARW_W + 16) { g_coll_page = (g_coll_page + np - 1) % np; g_coll_sel = -1; }
        else if (x >= WV_NEXT_X - 16) { g_coll_page = (g_coll_page + 1) % np; g_coll_sel = -1; }
        return COLL_TAP_KEPT;
    }
    if (x >= WV_X0 && x < WV_X0 + WV_COLS * WV_CW && y >= WV_Y0 && y < WV_Y0 + WV_ROWS * WV_CH) {
        const int i = g_coll_page * COLL_PAGE + (int)((y - WV_Y0) / WV_CH) * WV_COLS + (int)((x - WV_X0) / WV_CW);
        if (i < n) g_coll_sel = i;
    }
    return COLL_TAP_KEPT;
}
bool render_collection_arrow(bool right, int *x, int *y) {
    if (g_big_pocket) return render_collection_arrow_small(right, x, y);
    if (collection_pages() < 2) return false;
    if (x) *x = (right ? WV_NEXT_X : WV_PREV_X) + WST_ARW_W / 2;
    if (y) *y = WV_FOOT_Y + WST_CH / 2;
    return true;
}
/* (the tests, the shots) a visitor's cell on this page, frame px */
void render_big_visitor_where(int j, int *x, int *y) { *x = WV_X0 + (j % WV_COLS) * WV_CW + WV_CW / 2; *y = WV_Y0 + (j / WV_COLS) * WV_CH + WV_CH / 2; }

/* ---- MY TANKS's pages ----
 * REMOVE and the touch lock's note are the detail pane (the shelves stay beside them). THE LIBRARY and PICK A THEME are
 * one pane across the content: the little tanks at twice their size, the words at the wide pages' sizes; the rail
 * stays, dim - its MY TANKS is the way back, with BACK in the foot. RENAME's keyboard is the pocket page's own with
 * bigger keys (render.c NK_*, the TANK_P4 block). The library row's note is the pocket card, in the middle. */
#define WSM_BW      150                                  /* REMOVE: BACK; HOLD TO SELL takes the rest */
static void wsm_btns(int *bx, int *rx, int *rw) { *bx = WD_X + 24; *rx = *bx + WSM_BW + 12; *rw = WD_X + WD_W - 24 - *rx; }
int render_shelf_remove_hit(float x, float y) {
    if (g_big_pocket) return render_shelf_remove_hit_small(x, y);
    if (x < WD_X || x >= WD_X + WD_W || y < WC_Y || y >= WC_Y + WC_H) return SHM_TAP_OUTSIDE;
    int bx, rx, rw; wsm_btns(&bx, &rx, &rw);
    if (y >= WSC_BTN_Y - 12 && y < WSC_BTN_Y + WSC_BTN_H + 12) {
        if (x < bx + WSM_BW + 6) return SHM_TAP_BACK;
        return SHM_TAP_HOLD;
    }
    return SHM_TAP_NONE;
}
void render_shelf_remove(uint16_t *fb, int stride, const shelf_slot_t *s, int refund, const int n[3], float hold, int part) {
    if (g_big_pocket) { render_shelf_remove_small(fb, stride, s, refund, n, hold, part); return; }
    if (!(part & 2)) return;                               /* (no backdrop: the pane is drawn whole each frame) */
    ctx_t c = ctx_frame(fb, stride);
    const int CX = WD_X + WD_W / 2;
    w_pane(&c, WD_X, WD_W, MSP_TEAL);
    wsf_tank2(&c, CX - SH_TW, WSC_ART_Y, s->thumb, true);
    char nm[24]; snprintf(nm, sizeof nm, "%s", s->name); for (char *p = nm; *p; p++) if (*p == '\n') *p = ' ';
    wsh_mid(&c, WSC_NAME_Y - 4, 2, 0xffffff, "REMOVE");
    wsh_mid(&c, WSC_NAME_Y + 20, 4, 0xffffff, nm);
    wsh_mid(&c, WSC_NAME_Y + 60, 2, MSP_TEAL, "FROM THE SHELF?");
    for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(&c, x, WSC_DIV_Y - 30, MSP_DIM, 200);
    /* what the refund is for: render.c render_shelf_remove's words */
    static const char *const KN[3][2] = { { "PIECE", "PIECES" }, { "CRITTER", "CRITTERS" }, { "BACKGROUND", "BACKGROUNDS" } };
    const char *w3[3]; int m = 0;
    for (int k = 0; k < 3; k++) if (n && n[k] > 0) w3[m++] = KN[k][n[k] > 1];
    char t[40], t2[40] = "";
    if (m == 0) snprintf(t, sizeof t, "+%d FOR ITS PIECES", refund);
    else if (m == 1) snprintf(t, sizeof t, "+%d FOR ITS %s", refund, w3[0]);
    else if (m == 2) { snprintf(t, sizeof t, "+%d FOR ITS %s", refund, w3[0]); snprintf(t2, sizeof t2, "AND %s", w3[1]); }
    else { snprintf(t, sizeof t, "+%d FOR ITS %s,", refund, w3[0]); snprintf(t2, sizeof t2, "%s AND %s", w3[1], w3[2]); }
    if (g_sh_jelly) { snprintf(t, sizeof t, "+0"); snprintf(t2, sizeof t2, "MAKE A NEW ONE ANY TIME"); }
    else if (g_sh_xt) { snprintf(t, sizeof t, "+0"); snprintf(t2, sizeof t2, "GET IT AGAIN FOR FREE"); }
    char l1x[40]; snprintf(l1x, sizeof l1x, "THIS TANK AND ITS %s", g_sh_xt ? g_sh_xt : "");
    const char *l1 = g_sh_jelly ? "THIS TANK AND ITS JELLIES" : g_sh_xt ? l1x : "THIS TANK AND ITS FISH";
    int y = WSC_DIV_Y - 10;
    wsh_mid(&c, y, 2, 0xffffff, l1); y += 22;
    wsh_mid(&c, y, 2, 0xffffff, "ARE GONE FOR GOOD."); y += 34;
    { const int w = 16 + 6 + text_w(t, 2), x = CX - w / 2; blit_icon(&c, x, y - 1, &icon_shop_sand_dollar_16, 255); draw_text(&c, x + 22, y, 2, MSP_TEAL, t); }
    if (t2[0]) wsh_mid(&c, y + 22, 2, MSP_TEAL, t2);
    int bx, rx, rw; wsm_btns(&bx, &rx, &rw);
    button(&c, bx, WSC_BTN_Y, WSM_BW, WSC_BTN_H, W_BTN_FILL, MSP_TEAL, "BACK", 3);
    rect_fill(&c, rx, WSC_BTN_Y, rw, WSC_BTN_H, W_BTN_FILL);
    const int fw = (int)(rw * (hold < 0 ? 0 : hold > 1 ? 1 : hold));
    if (fw > 0) rect_fill(&c, rx, WSC_BTN_Y, fw, WSC_BTN_H, 0x2f7f8a);   /* the hold, filling */
    rect_edge(&c, rx, WSC_BTN_Y, rw, WSC_BTN_H, MSP_TEAL);
    draw_text(&c, rx + (rw - text_w("HOLD TO SELL", 3)) / 2, WSC_BTN_Y + (WSC_BTN_H - 21) / 2, 3, 0xffffff, "HOLD TO SELL");
}
void render_shelf_lock_note(uint16_t *fb, int stride) {
    if (g_big_pocket) { render_shelf_lock_note_small(fb, stride); return; }
    ctx_t c = ctx_frame(fb, stride);
    w_pane(&c, WD_X, WD_W, MSP_TEAL);
    int y = WC_Y + WC_H / 2 - 110;
    button(&c, WD_X + WD_W / 2 - 32, y, 64, 48, MSP_TEAL, MSP_TEAL, "", 2);
    sh_padlock(&c, WD_X + WD_W / 2, y + 25, true, MSP_INK, MSP_TEAL); y += 76;
    for (int k = 0; k < TLNOTE_N; k++, y += 24) if (TLNOTE[k][0]) wsh_mid(&c, y, 2, k >= 3 ? MSP_TEAL : 0xffffff, TLNOTE[k]);
    wsh_mid(&c, y + 20, 2, MSP_QUIET, "TAP ANYWHERE TO CLOSE");
}
/* THE LIBRARY */
#ifdef TANK_TAB5
#define WLB_PITCH   214
#else
#define WLB_PITCH   250
#endif
#define WLB_Y0      (WC_Y + 112)
#define WLB_X0      (WC_X + 40)
#define WLB_TX      (WLB_X0 + 2 * SH_TW + 28)
#define WLB_BW      220
#define WLB_BX      (WC_X + WC_W - 40 - WLB_BW)
#define WLB_BACK_W  160
#define WLB_BACK_X  (WC_X + WC_W - 40 - WLB_BACK_W)
#define WLB_NEW_W   220                                  /* PICK A THEME's NEW TANK */
static void wlb_rail(ctx_t *c) {                         /* the rail under a page of MY TANKS: its own button the way back, the rest dim */
    w_rail(c, WR_TANKS, false);
    for (int i = 0; i < WR_N; i++) if (i != WR_TANKS) button(c, WR_X, WR_Y0 + i * WR_DY, WR_W, WR_H, W_BTN_FILL, MSP_DIM, WR_WORDS[i], 3);
    button(c, WR_X, WR_CLOSE_Y, WR_W, WR_H, W_BTN_FILL, MSP_DIM, "CLOSE", 3);
}
static int wlb_wrap(ctx_t *c, int x, int y, int maxw, int pitch, int max_lines, uint32_t rgb, const char *s) {   /* sh_wrap at scale 2 */
    char line[64]; int n = 0, lines = 0, maxc = maxw / 12;
    if (maxc > 63) maxc = 63;
    while (*s && lines < max_lines) {
        const char *w = s; while (*w == ' ') w++;
        const char *e = w; while (*e && *e != ' ') e++;
        const int wl = (int)(e - w);
        if (n && n + 1 + wl > maxc) { line[n] = 0; draw_text(c, x, y, 2, rgb, line); y += pitch; lines++; n = 0; continue; }
        if (n) line[n++] = ' ';
        memcpy(line + n, w, (size_t)wl); n += wl; s = e;
    }
    if (n && lines < max_lines) { line[n] = 0; draw_text(c, x, y, 2, rgb, line); lines++; }
    return lines;
}
static void wlb_foot(ctx_t *c, int wallet, int page, int pages) {
    if (wallet >= 0) { char t[16]; snprintf(t, sizeof t, "%d", wallet);
        blit_icon(c, WLB_X0, WSF_FOOT_Y + (WSF_FOOT_H - 16) / 2, &icon_shop_sand_dollar_16, 255);
        draw_text(c, WLB_X0 + 24, WSF_FOOT_Y + (WSF_FOOT_H - 21) / 2, 3, 0xffffff, t); }
    if (pages > 1) { char t[16]; snprintf(t, sizeof t, "%d OF %d", page, pages);
        const int cx = WC_X + WC_W / 2, tw = text_w(t, 2);
        wst_arrow(c, cx - tw / 2 - 16 - WST_ARW_W, WSF_FOOT_Y, false, page > 1);
        draw_text(c, cx - tw / 2, WSF_FOOT_Y + (WSF_FOOT_H - 14) / 2, 2, 0xffffff, t);
        wst_arrow(c, cx + (tw + 1) / 2 + 16, WSF_FOOT_Y, true, page < pages); }
    button(c, WLB_BACK_X, WSF_FOOT_Y, WLB_BACK_W, WSF_FOOT_H, W_BTN_FILL, MSP_TEAL, "BACK", 3);
}
void render_library(uint16_t *fb, int stride, const library_row_t *row, int n, int wallet, const char *checked) {
    if (g_big_pocket) { render_library_small(fb, stride, row, n, wallet, checked); return; }
    ctx_t c = ctx_frame(fb, stride);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    wlb_rail(&c);
    w_pane(&c, WC_X, WC_W, MSP_TEAL);
    draw_text(&c, WC_X + (WC_W - text_w("THE LIBRARY", 4)) / 2, WC_Y + 22, 4, 0xffffff, "THE LIBRARY");
    draw_text(&c, WC_X + (WC_W - text_w(checked, 2)) / 2, WC_Y + 62, 2, MSP_QUIET, checked);
    for (int i = 0; i < n && i < 2; i++) {
        const library_row_t *r = &row[i];
        const int y = WLB_Y0 + i * WLB_PITCH, by = y + 28;
        wsf_tank2(&c, WLB_X0, y, r->thumb, r->locked);
        draw_text(&c, WLB_TX, y + 4, 4, r->locked ? MSP_QUIET : 0xffffff, r->name);
        const int ln = wlb_wrap(&c, WLB_TX, y + 48, WLB_BX - 28 - WLB_TX, 22, 3, r->locked ? MSP_QUIET : MSP_TEAL, r->line);
        const int tagy = y + 48 + ln * 22 + 10;
        if (r->experimental) { sh_flask(&c, WLB_TX, tagy + 3, MSP_QUIET); draw_text(&c, WLB_TX + 12, tagy, 2, MSP_QUIET, "EXPERIMENTAL"); }
        else if (r->installed) draw_text(&c, WLB_TX, tagy, 2, MSP_QUIET, "INSTALLED");
        const char *act = r->installed ? "NEW TANK" : "GET";
        if (i + 1 < n) for (int x = WLB_X0; x < WC_X + WC_W - 40; x++) px_blend(&c, x, y + WLB_PITCH - 24, MSP_DIM, 200);
        if (r->locked) {                                   /* not on the big screens yet (0.4.4): an outline, dim words, NOT YET under it -
                                                              a big screen is never on the crew (net_port_stub.c), so not CREW ONLY */
            button(&c, WLB_BX, by, WLB_BW, WSC_BTN_H, MSP_INK, MSP_DIM, "", 3);
            draw_text(&c, WLB_BX + (WLB_BW - text_w("GET", 3)) / 2, by + (WSC_BTN_H - 21) / 2, 3, MSP_DIM, "GET");
            draw_text(&c, WLB_BX + (WLB_BW - text_w("NOT YET", 2)) / 2, by + WSC_BTN_H + 12, 2, MSP_QUIET, "NOT YET");
            continue;
        }
        if (r->full) {                                     /* one is all a board keeps: nothing to tap */
            button(&c, WLB_BX, by, WLB_BW, WSC_BTN_H, MSP_INK, MSP_DIM, "", 3);
            draw_text(&c, WLB_BX + (WLB_BW - text_w("INSTALLED", 3)) / 2, by + (WSC_BTN_H - 21) / 2, 3, MSP_DIM, "INSTALLED");
            continue;
        }
        if (r->price > wallet) { button(&c, WLB_BX, by, WLB_BW, WSC_BTN_H, MSP_INK, MSP_DIM, "", 3);
                                 draw_text(&c, WLB_BX + (WLB_BW - text_w(act, 3)) / 2, by + (WSC_BTN_H - 21) / 2, 3, MSP_DIM, act); }
        else { button(&c, WLB_BX, by, WLB_BW, WSC_BTN_H, MSP_TEAL, MSP_TEAL, "", 3);
               draw_text(&c, WLB_BX + (WLB_BW - text_w(act, 3)) / 2, by + (WSC_BTN_H - 21) / 2, 3, MSP_INK, act); }
        if (r->price) { char t[16]; snprintf(t, sizeof t, "%d", r->price);
            const int w = 16 + 6 + text_w(t, 2), px0 = WLB_BX + (WLB_BW - w) / 2;
            blit_icon(&c, px0, by + WSC_BTN_H + 11, &icon_shop_sand_dollar_16, 255);
            draw_text(&c, px0 + 22, by + WSC_BTN_H + 12, 2, 0xffffff, t);
        } else draw_text(&c, WLB_BX + (WLB_BW - text_w("FREE", 2)) / 2, by + WSC_BTN_H + 12, 2, 0xffffff, "FREE");
    }
    wlb_foot(&c, wallet, g_lib_page, g_lib_pages);
}
int render_library_tap(float x, float y, int n) {
    if (g_big_pocket) return render_library_tap_small(x, y, n);
    {   /* the rail: MY TANKS (its own button) and CLOSE both go one step back, as BACK does (Strato 11-23: "the close button
           should do the same thing back does" - it fell through as no tap). Back to MY TANKS, not out: main.c's LIB_TAP_BACK
           puts the main page back with its open wait (R2T-5), so a doubled CLOSE does not reach MY TANKS's CLOSE under it */
        const int rh = w_rail_hit(x, y);
        if (rh == WR_TANKS || rh == WR_N) return LIB_TAP_BACK;
    }
    if (x < WC_X) return LIB_TAP_NONE;
    if (y >= WSF_FOOT_Y - 12) {
        if (x >= WLB_BACK_X - 12) return LIB_TAP_BACK;
        const int cx = WC_X + WC_W / 2;
        if (g_lib_pages > 1) { if (x >= cx - 130 && x < cx - 30) return LIB_TAP_PREV; if (x >= cx + 30 && x < cx + 130) return LIB_TAP_NEXT; }
        return LIB_TAP_NONE;
    }
    for (int i = 0; i < n && i < 2; i++) {
        const int by = WLB_Y0 + i * WLB_PITCH + 28;
        if (x >= WLB_BX - 12 && y >= by - 24 && y < by + WSC_BTN_H + 40) return LIB_TAP_ROW0 + i;   /* the button and the price under it */
    }
    return LIB_TAP_NONE;
}
void render_library_pic_rect(int row, int *x, int *y, int *w, int *h) {
    if (g_big_pocket) { render_library_pic_rect_small(row, x, y, w, h); return; }
    *x = WLB_X0 + 4; *y = WLB_Y0 + row * WLB_PITCH + 2 * SH_LID; *w = 2 * SH_PIC_W; *h = 2 * SH_IH;
}
void render_library_pic(uint16_t *fb, int stride, int row, const uint16_t *pic, bool locked) {
    if (g_big_pocket) { render_library_pic_small(fb, stride, row, pic, locked); return; }
    ctx_t c = ctx_frame(fb, stride);
    wsf_tank2(&c, WLB_X0, WLB_Y0 + row * WLB_PITCH, pic, locked);
}
void render_library_loading(uint16_t *fb, int stride, unsigned rows, unsigned locked, float t) {
    if (g_big_pocket) { render_library_loading_small(fb, stride, rows, locked, t); return; }
    ctx_t c = ctx_frame(fb, stride);
    for (int i = 0; i < 2; i++) if (rows >> i & 1) { int x, y, w, h; render_library_pic_rect(i, &x, &y, &w, &h); sh_loading(&c, x, y, w, h, t, locked >> i & 1); }
}
/* PICK A THEME */
#define WJP_CW      (WC_W / 3)
#define WJP_Y0      (WC_Y + 120)
static int wjp_x(int k) { return WC_X + k * WJP_CW + (WJP_CW - 2 * SH_TW) / 2; }
void render_jelly_pick(uint16_t *fb, int stride, const char *const names[3], const uint16_t *const thumbs[3], int picked) {
    if (g_big_pocket) { render_jelly_pick_small(fb, stride, names, thumbs, picked); return; }
    ctx_t c = ctx_frame(fb, stride);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    wlb_rail(&c);
    w_pane(&c, WC_X, WC_W, MSP_TEAL);
    draw_text(&c, WC_X + (WC_W - text_w("PICK A THEME", 4)) / 2, WC_Y + 22, 4, 0xffffff, "PICK A THEME");
    const int TW2 = 2 * SH_TW, BH2 = 2 * SH_BOX_H;
    for (int k = 0; k < 3; k++) {
        const int bx = wjp_x(k); const bool on = k == picked;
        if (on) { rect_edge(&c, bx - 8, WJP_Y0 - 8, TW2 + 16, BH2 + 16, 0xffffff); rect_edge(&c, bx - 7, WJP_Y0 - 7, TW2 + 14, BH2 + 14, 0xffffff);
                  rect_edge(&c, bx - 5, WJP_Y0 - 5, TW2 + 10, BH2 + 10, MSP_TEAL); }
        wsf_tank2(&c, bx, WJP_Y0, thumbs[k], picked >= 0 && !on);
        draw_text(&c, WC_X + k * WJP_CW + (WJP_CW - text_w(names[k], 3)) / 2, WJP_Y0 + BH2 + 22, 3, on || picked < 0 ? 0xffffff : MSP_QUIET, names[k]);
    }
    const int MX = WC_X + WC_W / 2; int y = WJP_Y0 + BH2 + 74;
    const char *l1 = "THE THEME STAYS WITH THIS TANK FOR GOOD.", *e1 = "STILL BEING BUILT. IT MAY CHANGE, AND MAY START OVER.";
    draw_text(&c, MX - text_w(l1, 2) / 2, y, 2, 0xffffff, l1); y += 32;
    sh_flask(&c, MX - text_w(e1, 2) / 2 - 14, y + 3, MSP_QUIET);
    draw_text(&c, MX - text_w(e1, 2) / 2, y, 2, MSP_QUIET, e1);
    if (picked >= 0) { button(&c, WLB_X0, WSF_FOOT_Y, WLB_NEW_W, WSF_FOOT_H, MSP_TEAL, MSP_TEAL, "", 3);
                       draw_text(&c, WLB_X0 + (WLB_NEW_W - text_w("NEW TANK", 3)) / 2, WSF_FOOT_Y + (WSF_FOOT_H - 21) / 2, 3, MSP_INK, "NEW TANK"); }
    else { button(&c, WLB_X0, WSF_FOOT_Y, WLB_NEW_W, WSF_FOOT_H, MSP_INK, MSP_DIM, "", 3);
           draw_text(&c, WLB_X0 + (WLB_NEW_W - text_w("NEW TANK", 3)) / 2, WSF_FOOT_Y + (WSF_FOOT_H - 21) / 2, 3, MSP_DIM, "NEW TANK"); }
    wlb_foot(&c, -1, 1, 1);
}
int render_jelly_pick_tap(float x, float y) {
    if (g_big_pocket) return render_jelly_pick_tap_small(x, y);
    if (w_rail_hit(x, y) == WR_TANKS) return JP_TAP_BACK;
    if (x < WC_X) return JP_TAP_NONE;
    if (y >= WJP_Y0 - 14 && y < WJP_Y0 + 2 * SH_BOX_H + 56 && x < WC_X + 3 * WJP_CW) return JP_TAP_THEME0 + (int)((x - WC_X) / WJP_CW);   /* the picture or its name */
    if (y >= WSF_FOOT_Y - 12) {
        if (x < WLB_X0 + WLB_NEW_W + 12) return JP_TAP_NEW;
        if (x >= WLB_BACK_X - 12) return JP_TAP_BACK;
    }
    return JP_TAP_NONE;
}
void render_jelly_pick_pic_rect(int k, int *x, int *y, int *w, int *h) {
    if (g_big_pocket) { render_jelly_pick_pic_rect_small(k, x, y, w, h); return; }
    *x = wjp_x(k) + 4; *y = WJP_Y0 + 2 * SH_LID; *w = 2 * SH_PIC_W; *h = 2 * SH_IH;
}
void render_jelly_pick_pic(uint16_t *fb, int stride, int k, const uint16_t *pic, int picked) {
    if (g_big_pocket) { render_jelly_pick_pic_small(fb, stride, k, pic, picked); return; }
    ctx_t c = ctx_frame(fb, stride);
    wsf_tank2(&c, wjp_x(k), WJP_Y0, pic, picked >= 0 && k != picked);
}
void render_jelly_pick_loading(uint16_t *fb, int stride, unsigned themes, int picked, float t) {
    if (g_big_pocket) { render_jelly_pick_loading_small(fb, stride, themes, picked, t); return; }
    ctx_t c = ctx_frame(fb, stride);
    for (int k = 0; k < 3; k++) { if (!(themes >> k & 1)) continue;
        int x, y, w, h; render_jelly_pick_pic_rect(k, &x, &y, &w, &h); sh_loading(&c, x, y, w, h, t + k * 0.9f, picked >= 0 && k != picked); }
}
/* THE EXPERIMENTAL TANKS' NOTE (0.4.3, Strato 11-23: "yes build the note, move the tanks to 0.4.4"). The jelly tank, the
   piranhas and the minnows do not run on the big screens yet (docs/home-tank-0.4.3.md "The experimental tanks: 0.4.4"), and
   no big screen can be on the crew (every big build links net_port_stub.c), so their rows read NOT YET under the dimmed GET
   and a tap on one opens this: the pocket note's look - the tank's name by the flask, two lines, OK - drawn at 1x, then put
   back at twice its size on its own centre, as card_2x puts the notices (Strato 11-22 "yes double size is good"). Its
   scratch refused: the card stays at 1x, and so does its tap */
static const char *const XNOTE[] = { "NOT AVAILABLE ON", "HOME TANK YET." };
#define XNOTE_N    ((int)(sizeof XNOTE / sizeof XNOTE[0]))
#define XNOTE_H1   (16 + 22 + 10 + XNOTE_N * LNOTE_LH + 12 + MSP_CLOSE_H + 16)   /* (the plain card's height) */
static int g_xnote_xl;                                   /* lines past the plain card's two, as last drawn (the restart chain's card: 1) */
#define XNOTE_H    (XNOTE_H1 + g_xnote_xl * LNOTE_LH)
#define XNOTE_X    ((TANK_W - SHC_W) / 2)
#define XNOTE_Y    ((TANK_H - XNOTE_H) / 2)
#define XNOTE_OK_W 98
static bool g_xnote_2x = true;                           /* (the card as last drawn: at 2x, or at 1x - its scratch refused) */
static __attribute__((noinline)) void xnote_map(int *x, int *y) {                  /* a point of the 1x card: where it is on the glass */
    if (g_xnote_2x) { *x = XNOTE_X - SHC_W / 2 + 2 * (*x - XNOTE_X); *y = XNOTE_Y - XNOTE_H / 2 + 2 * (*y - XNOTE_Y); }
}
bool render_big_library_row_note(int page, int row) {   /* page 1: the fish tank, THE JELLY TANK; page 2: THE PIRANHAS, THE MINNOWS */
    return page == 1 ? row == 0 || row == 1 : row == 1;
}
/* the note's card (THE LIBRARY's NOT YET, THE LINK DID NOT START): the tank dimmed, the ink card, its double teal edge,
   a title (the library's with its flask), two lines, OK - drawn at 1x and doubled in place (its scratch refused: 1x) */
/* the tank dimmed under the card, a row at a time (11-24): px565_blend(c, x, y, 0, 150) on every pixel made each of the
   million pixels a call and a tile signature - LINK's cards cost the frame ~18 times the tank's own render. The same
   arithmetic (tabled per channel), the row signed once per tile, marked as px565_blend marks it */
static void xnote_dim_row(ctx_t *c, int y) {
    static uint8_t d5[32], d6[64]; static bool made;
    if (!made) { for (int v = 0; v < 64; v++) { if (v < 32) d5[v] = (uint8_t)(v + ((0 - v) * 150 >> 8)); d6[v] = (uint8_t)(v + ((0 - v) * 150 >> 8)); } made = true; }
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    const int x0 = c->ox, x1 = c->ox + c->w - 1;
    OV_SPAN(c, x0, x1, y, 0x565d0096u);
    uint16_t *p = &CTX_PX(c, x0, y);
    for (int x = x0; x <= x1; ) {
        const int e0 = x - (x - c->ox) % RENDER_TILE + RENDER_TILE - 1, e = e0 > x1 ? x1 : e0;   /* the tile's last px on this row */
        if (!OV_AT(c, x, y)) { p += e - x + 1; x = e + 1; continue; }
        for (; x <= e; x++, p++) { const uint16_t cur = FB_SWAP16(*p);
            *p = FB_SWAP16((uint16_t)(d5[cur >> 11] << 11 | d6[(cur >> 5) & 63] << 5 | d5[cur & 31])); }
    }
    bb_add(x0, x1, y); TOUCH_SPAN(c, x0, x1, y);
}
static void xnote_card_n(uint16_t *fb, int stride, const char *title, bool flask, const char *const *L, int n, uint32_t lines_rgb, int bar_pct) {
    g_xnote_xl = n > 2 ? n - 2 : 0;
    ctx_t c = ctx_frame(fb, stride);
    for (int y = 0; y < TANK_H; y++) xnote_dim_row(&c, y);
    const int mx = XNOTE_X, my = XNOTE_Y;
    rect_fill(&c, mx, my, SHC_W, XNOTE_H, MSP_INK);
    rect_edge(&c, mx, my, SHC_W, XNOTE_H, MSP_TEAL); rect_edge(&c, mx + 1, my + 1, SHC_W - 2, XNOTE_H - 2, MSP_TEAL);
    int y = my + 16; const int tw = text_w(title, 2) + (flask ? 12 : 0);
    if (flask) sh_flask(&c, mx + (SHC_W - tw) / 2, y + 2, 0xffffff);
    draw_text(&c, mx + (SHC_W - tw) / 2 + (flask ? 12 : 0), y, 2, 0xffffff, title); y += 22 + 10;
    for (int k = 0; k < (n > 2 ? n : 2); k++, y += LNOTE_LH) if (k < n && L[k]) draw_text(&c, mx + (SHC_W - text_w(L[k], 2)) / 2, y, 2, lines_rgb, L[k]);
    if (bar_pct < 0) lit_button(&c, mx + (SHC_W - XNOTE_OK_W) / 2, y + 12, XNOTE_OK_W, MSP_CLOSE_H, "OK");
    else {                                               /* (UPDATING YOUR DEVICE) a bar where OK would be: no button - the update is not stopped from here */
        const int bw = SHC_W - 64, bx = mx + 32, by = y + 12 + (MSP_CLOSE_H - 14) / 2, p = bar_pct > 100 ? 100 : bar_pct;
        rect_edge(&c, bx, by, bw, 14, MSP_TEAL);
        if (p > 0) rect_fill(&c, bx + 2, by + 2, (bw - 4) * p / 100, 10, MSP_TEAL);
    }
    /* at twice its size: the card's own pixels (344 x 142 x 2 = 98 KB, on the heap - PSRAM on the boards, as card_2x's) */
    static uint16_t *tmp;
    g_xnote_2x = !g_card2x_fail && (tmp || (tmp = malloc(sizeof *tmp * SHC_W * (XNOTE_H1 + LNOTE_LH))));   /* (room for the taller card) */
    if (!g_xnote_2x) return;
    for (int r = 0; r < XNOTE_H; r++) memcpy(tmp + r * SHC_W, fb + (size_t)(my + r) * stride + mx, sizeof *tmp * SHC_W);
    const int X2 = mx - SHC_W / 2, Y2 = my - XNOTE_H / 2;
    for (int r = 0; r < 2 * XNOTE_H; r++) {
        uint16_t *d = fb + (size_t)(Y2 + r) * stride + X2; const uint16_t *s = tmp + (r / 2) * SHC_W;
        for (int q = 0; q < SHC_W; q++) { d[0] = d[1] = s[q]; d += 2; }
    }
    /* The doubled card is copied around the primitives, so its box is signed here (render.c THE OVERLAY TILES) with
       what the card shows. The card is opaque, so its words, its flask, its color and its bar are all of it. Then
       LINK's cards can go as overlay tiles over the live tank, and a moving bar is turned where it moves (11-24). */
    {   uint32_t h = 0x9e3779b1u;
        for (int k = -1; k < n; k++) for (const char *q = (k < 0 ? title : L[k]) ? (k < 0 ? title : L[k]) : ""; ; q++) { h = OV_MIX(h, (uint8_t)*q); if (!*q) break; }
        h = OV_MIX(h, (uint32_t)n);                     /* (the lines, any number of them: the chain card's three) */
        h = OV_MIX(OV_MIX(OV_MIX(h, (uint32_t)flask), lines_rgb), (uint32_t)(bar_pct < 0 ? -1 : bar_pct > 100 ? 100 : bar_pct));
        OV_RECT(fb, X2, Y2, X2 + 2 * SHC_W - 1, Y2 + 2 * XNOTE_H - 1, h); }
}
static __attribute__((noinline)) void xnote_card(uint16_t *fb, int stride, const char *title, bool flask, const char *l0, const char *l1, uint32_t lines_rgb, int bar_pct) {
    const char *const L[2] = { l0, l1 }; xnote_card_n(fb, stride, title, flask, L, 2, lines_rgb, bar_pct);
}
void render_library_note(uint16_t *fb, int stride) {
    if (g_big_pocket) { render_library_note_small(fb, stride); return; }
    xnote_card(fb, stride, g_lnote_name, true, XNOTE[0], XNOTE[1], 0xffffff, -1);
}
/* THE LINK DID NOT START (0.4.3, Strato 11-23: "sure those words work"): LINK tapped and the big screen's radio could
   not start (radio_hosted_why: no room for it, the stack would not start, the C6 did not answer) - link mode's page
   opens as this note (link_dev.c). What to do in teal, as the radio's other cards (the design's F cards). OK or a
   tap outside closes it, and the tank goes on: nothing restarts. The NOT YET note's card, size and OK. */
static const char *const LKNOTE[] = { "THE LINK DID NOT START", "GET HELP AT", "POCKETANK.COM/HELP" };
const char *render_big_link_note_line(int i) { return i >= 0 && i < (int)(sizeof LKNOTE / sizeof LKNOTE[0]) ? LKNOTE[i] : NULL; }
void render_link_nostart(uint16_t *fb, int stride) { xnote_card(fb, stride, LKNOTE[0], false, LKNOTE[1], LKNOTE[2], MSP_TEAL, -1); }
/* STARTING THE LINK (0.4.3, the 11-24 radio audit; approved by Strato 11-25): LINK tapped, and the radio task is
   starting the C6 - seconds on a slow answer, and the tank goes on meanwhile (link_note.h). The same card and OK: OK
   or a tap outside closes it (the start goes on, and is taken down again if nobody waits for it). */
static const char *const LKWAIT[] = { "STARTING THE LINK", "THIS CAN TAKE", "UP TO A MINUTE" };   /* (a few seconds on our relay; a C6 that answers nothing runs every request out: ~30 s) */
const char *render_big_link_wait_line(int i) { return i >= 0 && i < (int)(sizeof LKWAIT / sizeof LKWAIT[0]) ? LKWAIT[i] : NULL; }
void render_link_wait(uint16_t *fb, int stride) { xnote_card(fb, stride, LKWAIT[0], false, LKWAIT[1], LKWAIT[2], MSP_TEAL, -1); }
/* THE RESTART CHAIN's card (0.4.3; Strato 11-25, words and look approved): LINK tapped with the update in the radio chip
   and the chip deaf to it - the tank restarts itself, both chips, until the chip's guard decides (c6_ho.h, the escape
   chain). On the tap before the first restart (held ~3 s to read) and at once on every boot of the chain. STARTING
   THE LINK's card, its first line wrapped at the usual size: the card one line taller, its OK lower (render_link_escape_tap) */
static const char *const LKESC[] = { "STARTING LINK", "YOUR DEVICE MAY RESTART", "SEVERAL TIMES", "THIS IS NORMAL" };   /* (line 1 wrapped where the
                                                       card's width wraps it, as approved: the selftest checks it fits and reads as one line) */
const char *render_big_link_esc_line(int i) { return i >= 0 && i < 4 ? LKESC[i] : NULL; }
void render_link_escape(uint16_t *fb, int stride) { xnote_card_n(fb, stride, LKESC[0], false, LKESC + 1, 3, MSP_TEAL, -1); }
/* the one install's handover (0.4.3; Strato 11-24 "automatic"; the words approved 11-25): the staged relay going into the
   radio chip by itself - UPDATING YOUR DEVICE with a bar and no button (it cannot be stopped from here; the C6 keeps its
   old firmware if the power goes), and THE LINK UPDATE STOPPED if the write did not finish (OK closes it; LINK tries
   again). Success has no card: the tank saves and both chips restart. */
static const char *const LKUPD[] = { "UPDATING YOUR DEVICE", "KEEP PLUGGED IN", "" };   /* (Strato 11-25) */
static const char *const LKUPDNO[] = { "THE LINK UPDATE STOPPED", "THE TANK IS FINE", "TAP LINK TO TRY AGAIN" };
/* (the one-install audit's H1; approved 11-25) the same title when no try is left: the DID NOT START card's
   help lines, never a retry that does not exist (link_note.h LN_UPD_OUT) */
static const char *const LKUPDOUT[] = { "THE LINK UPDATE STOPPED", "GET HELP AT", "POCKETANK.COM/HELP" };
const char *render_big_link_upd_line(int fail, int i) { const char *const *w = fail == 2 ? LKUPDOUT : fail ? LKUPDNO : LKUPD; return i >= 0 && i < 3 ? w[i] : NULL; }
void render_link_updating(uint16_t *fb, int stride, int pct) { xnote_card(fb, stride, LKUPD[0], false, LKUPD[1], LKUPD[2], MSP_TEAL, pct < 0 ? 0 : pct); }
void render_link_update_failed(uint16_t *fb, int stride, bool retry) { const char *const *w = retry ? LKUPDNO : LKUPDOUT; xnote_card(fb, stride, w[0], false, w[1], w[2], MSP_TEAL, -1); }
void render_big_library_note_where(int what, int *x, int *y) {   /* OK's middle; the card's top-left and bottom-right pixels */
    if (what == BIGLN_OK) { *x = XNOTE_X + SHC_W / 2; *y = XNOTE_Y + XNOTE_H - 16 - MSP_CLOSE_H / 2; }
    else if (what == BIGLN_TL) { *x = XNOTE_X; *y = XNOTE_Y; }
    else { *x = XNOTE_X + SHC_W; *y = XNOTE_Y + XNOTE_H; }
    xnote_map(x, y);
    if (what == BIGLN_BR) { --*x; --*y; }
}
static int xnote_tap(float x, float y) {
    int x0, y0, x1, y1; render_big_library_note_where(BIGLN_TL, &x0, &y0); render_big_library_note_where(BIGLN_BR, &x1, &y1);
    if (x < x0 || x > x1 || y < y0 || y > y1) return LNOTE_TAP_OUTSIDE;
    int bx = XNOTE_X + (SHC_W - XNOTE_OK_W) / 2, by = XNOTE_Y + XNOTE_H - 16 - MSP_CLOSE_H, bx1 = bx + XNOTE_OK_W, by1 = by + MSP_CLOSE_H;
    xnote_map(&bx, &by); xnote_map(&bx1, &by1);
    const int m = g_xnote_2x ? 16 : 8;                  /* (the pocket note's reach round OK, at the card's size) */
    return x >= bx - m && x < bx1 + m && y >= by - m && y < by1 + m ? LNOTE_TAP_OK : LNOTE_TAP_NONE;
}
int render_library_note_tap(float x, float y) { return g_big_pocket ? render_library_note_tap_small(x, y) : xnote_tap(x, y); }
int render_link_nostart_tap(float x, float y) { return xnote_tap(x, y); }   /* LNOTE_TAP_*: the same card, the same OK (the card as last drawn:
                                                                               the restart chain's taller one too) */
void render_big_link_note_where(int what, int *x, int *y) { render_big_library_note_where(what, x, y); }
/* (the tests, the shots) THE LIBRARY's row button, its BACK; PICK A THEME's tank k, NEW TANK */
void render_big_library_where(int what, int *x, int *y) {
    *y = WSF_FOOT_Y + WSF_FOOT_H / 2;
    if (what == BIGLIB_BACK) *x = WLB_BACK_X + WLB_BACK_W / 2;
    else if (what == BIGLIB_NEW) *x = WLB_X0 + WLB_NEW_W / 2;
    else if (what >= BIGLIB_THEME0) { *x = wjp_x(what - BIGLIB_THEME0) + SH_TW; *y = WJP_Y0 + SH_BOX_H; }
    else { *x = WLB_BX + WLB_BW / 2; *y = WLB_Y0 + what * WLB_PITCH + 28 + WSC_BTN_H / 2; }
}

/* ---- pieces for a page drawn in another file (update.c's UPDATES), and for a page that stands still ----
 * A STILL PAGE (ADVANCED, UPDATES): nothing on it moves, so a buffer that holds it under the same key is left alone
 * and the page says "no boxes" (render_milestones_changed 0: the port turns nothing). render_big_still_drawn: this
 * frame's page went through here (main.c: only then is ADVANCED a kept page - its other pages are drawn whole). */
_Static_assert(BIG_WL_X == WL_X && BIG_WL_W == WL_W && BIG_WD_X == WD_X && BIG_WD_W == WD_W && BIG_WC_Y == WC_Y && BIG_WC_H == WC_H && BIG_WC_X == WC_X && BIG_WC_W == WC_W, "render.h's copy of the wide frame");
static unsigned g_wstill_frame = ~0u;
bool render_big_pocket(void) { return g_big_pocket; }
bool render_big_still_drawn(void) { return g_wstill_frame == g_rframe; }
bool render_big_still_begin(uint16_t *fb, int stride, uint32_t key) {
    g_wstill_frame = g_rframe;
    if (!g_ms_whole_dbg) {
        if (g_wk_frame != g_rframe && g_wk_frame + 1 != g_rframe) g_wk_run0 = g_rframe;
        g_wk_frame = g_rframe;
    }
    wk_t *kp = g_wk[0].fb == fb ? &g_wk[0] : g_wk[1].fb == fb ? &g_wk[1] : g_wk[0].frame <= g_wk[1].frame ? &g_wk[0] : &g_wk[1];
    const bool keep = !g_ms_whole_dbg && kp->fb == fb && kp->ok && kp->key == key && kp->other == g_ctx_other && stride == TANK_W
                      && kp->frame < g_rframe && kp->frame >= g_wk_run0;
    g_dirty_hold = true;
    if (keep) { kp->frame = g_rframe; g_wk_kept_n++; g_wk_n = 0; return true; }
    g_wk_whole_n++; g_wk_n = -1; g_ms_in = true;
    return false;
}
void render_big_still_end(uint16_t *fb, uint32_t key) {
    g_ms_in = false; g_wk_n = -1;
    if (g_ms_whole_dbg) return;
    wk_t *kp = g_wk[0].fb == fb ? &g_wk[0] : g_wk[1].fb == fb ? &g_wk[1] : g_wk[0].frame <= g_wk[1].frame ? &g_wk[0] : &g_wk[1];
    kp->fb = fb; kp->key = key; kp->ok = true; kp->frame = g_rframe; kp->other = g_ctx_other;
}
void render_big_frame_sub(uint16_t *fb, int stride, int lit) {      /* the ink, the rail with `lit` lit and the rest dim (a page under it) */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    w_rail(&c, lit, false);
    for (int i = 0; i < WR_N; i++) if (i != lit) button(&c, WR_X, WR_Y0 + i * WR_DY, WR_W, WR_H, W_BTN_FILL, MSP_DIM, WR_WORDS[i], 3);
    button(&c, WR_X, WR_CLOSE_Y, WR_W, WR_H, W_BTN_FILL, MSP_DIM, "CLOSE", 3);
}
void render_big_pane(uint16_t *fb, int stride, int x, int w, bool strong) { ctx_t c = ctx_full(fb, stride, 1.0f); w_pane(&c, x, w, strong ? MSP_TEAL : 0x2c4a52); }
int  render_big_rail_hit(float x, float y) { return w_rail_hit(x, y); }

/* ---- ADVANCED (the page behind BOOT held + a tap on SETTINGS) ----
 * SETTINGS' two columns: TOUCH (CALIBRATE) and BATTERY (the size between two arrows, SET while the pick is not the
 * size in use). The rail stays, dim: SETTINGS is the way back, with BACK in the foot. A still page. The warning, the
 * touch pages and the crosses are the pocket pages still. */
#define WAD_RW      (WC_X + WC_W - 36 - WST_RX)            /* the right column's width */
#define WAD_SET_Y   (WST_CY(0) + 124)
#define WAD_BACK_W  160
#define WAD_BACK_X  (WC_X + WC_W - 36 - WAD_BACK_W)
#define WAD_BACK_Y  (WC_Y + WC_H - 24 - WST_CH)
void render_advanced(uint16_t *fb, int stride, int pick_mah, int pick_ma, int set_mah, bool stock, bool fast) {
    if (g_big_pocket) { g_wk[0].ok = g_wk[1].ok = false; g_wk_n = -1; render_advanced_small(fb, stride, pick_mah, pick_ma, set_mah, stock, fast); return; }
    uint32_t key = 2166136261u;
    key = WK_MIX(key, 0xad); key = WK_MIX(key, pick_mah); key = WK_MIX(key, pick_ma); key = WK_MIX(key, set_mah); key = WK_MIX(key, stock); key = WK_MIX(key, fast);
    if (render_big_still_begin(fb, stride, key)) return;
    render_big_frame_sub(fb, stride, WR_SETTINGS);
    ctx_t c = ctx_full(fb, stride, 1.0f);
    char txt[40];
    w_pane(&c, WC_X, WC_W, MSP_TEAL);
    draw_text(&c, WC_X + (WC_W - text_w("ADVANCED", 4)) / 2, WC_Y + 22, 4, 0xffffff, "ADVANCED");
    draw_text(&c, WC_X + (WC_W - text_w("FOR GROWN-UPS", 2)) / 2, WC_Y + 62, 2, MSP_QUIET, "FOR GROWN-UPS");
    /* TOUCH */
    draw_text(&c, WST_LX, WST_BY(0), 2, MSP_TEAL, "TOUCH");
    button(&c, WST_LX, WST_CY(0), WST_BTN_W, WST_CH, W_BTN_FILL, MSP_TEAL, "CALIBRATE", 2);
    draw_text(&c, WST_LX, WST_CY(0) + WST_CH + 12, 2, MSP_QUIET, "TAPS MISSING THEIR MARK?");
    /* BATTERY: < 800 MAH > */
    const int y = WST_CY(0), bx = WST_RX + WST_ARW_W + 8, bw = WAD_RW - 2 * (WST_ARW_W + 8);
    const int *sz; const int n = adv_cell_sizes(&sz);
    draw_text(&c, WST_RX, WST_BY(0), 2, MSP_TEAL, "BATTERY");
    wst_arrow(&c, WST_RX, y, false, pick_mah > sz[0]);
    button(&c, bx, y, bw, WST_CH, MSP_TEAL, MSP_TEAL, "", 2);
    snprintf(txt, sizeof txt, "%d MAH", pick_mah);
    const int tw = text_w(txt, 3), sx = bx + (bw - tw - (fast ? 26 : 0)) / 2;
    if (fast) adv_warn_sign(&c, sx + 8, y + (WST_CH - 15) / 2, 17, MSP_INK);   /* faster than the board's own: the sign rides along */
    draw_text(&c, sx + (fast ? 26 : 0), y + (WST_CH - 21) / 2, 3, MSP_INK, txt);
    wst_arrow(&c, WST_RX + WAD_RW - WST_ARW_W, y, true, pick_mah < sz[n - 1]);
    if (stock) snprintf(txt, sizeof txt, "STANDARD - CHARGES AT %d MA", pick_ma);
    else       snprintf(txt, sizeof txt, "CHARGES AT %d MA", pick_ma);
    draw_text(&c, WST_RX, y + WST_CH + 12, 2, MSP_QUIET, txt);
    if (pick_mah != set_mah) {                    /* a size picked, not in use yet: SET applies it (the warning first, when it is faster) */
        snprintf(txt, sizeof txt, "NOW %d MAH", set_mah);
        draw_text(&c, WST_RX, y + WST_CH + 36, 2, MSP_QUIET, txt);
        snprintf(txt, sizeof txt, "SET %d MAH", pick_mah);
        button(&c, WST_RX, WAD_SET_Y, WAD_RW, WSC_BTN_H, MSP_TEAL, MSP_TEAL, "", 3);
        draw_text(&c, WST_RX + (WAD_RW - text_w(txt, 3)) / 2, WAD_SET_Y + (WSC_BTN_H - 21) / 2, 3, MSP_INK, txt);
    } else draw_text(&c, WST_RX, y + WST_CH + 36, 2, MSP_QUIET, "MATCH THE SIZE ON YOUR BATTERY");
    button(&c, WAD_BACK_X, WAD_BACK_Y, WAD_BACK_W, WST_CH, W_BTN_FILL, MSP_TEAL, "BACK", 3);
    render_big_still_end(fb, key);
}
int render_advanced_tap(float x, float y) {
    if (g_big_pocket) return render_advanced_tap_small(x, y);
    if (w_rail_hit(x, y) == WR_SETTINGS) return ADV_HIT_BACK;
    if (x < WC_X) return ADV_HIT_NONE;
    if (y >= WAD_BACK_Y - 12 && x >= WAD_BACK_X - 12) return ADV_HIT_BACK;
    if (y >= WST_CY(0) - 12 && y < WST_CY(0) + WST_CH + 12) {
        if (x < WST_LX + WST_BTN_W + 16) return ADV_HIT_CALIBRATE;
        if (x >= WST_RX - 12) return x < WST_RX + WAD_RW / 2 ? ADV_HIT_PREV : ADV_HIT_NEXT;   /* the row's halves, as the pocket page's */
        return ADV_HIT_NONE;
    }
    if (x >= WST_RX - 12 && y >= WAD_SET_Y - 8 && y < WAD_SET_Y + WSC_BTN_H + 12) return ADV_HIT_SET;   /* (only while it is drawn: advanced.c asks) */
    return ADV_HIT_NONE;
}
void render_big_advanced_where(int hit, int *x, int *y) {      /* (the tests) a control's middle, frame px */
    *y = WST_CY(0) + WST_CH / 2;
    switch (hit) {
    case ADV_HIT_CALIBRATE: *x = WST_LX + WST_BTN_W / 2; break;
    case ADV_HIT_PREV: *x = WST_RX + WST_ARW_W / 2; break;
    case ADV_HIT_NEXT: *x = WST_RX + WAD_RW - WST_ARW_W / 2; break;
    case ADV_HIT_SET: *x = WST_RX + WAD_RW / 2; *y = WAD_SET_Y + WSC_BTN_H / 2; break;
    default: *x = WAD_BACK_X + WAD_BACK_W / 2; *y = WAD_BACK_Y + WST_CH / 2; break;
    }
}
