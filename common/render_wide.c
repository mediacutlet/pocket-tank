/* NOT BUILT in 0.4.3 (docs/home-tank-0.4.3.md): this file is October's, as it was - render.c does not include it and its
 * small pages are not fenced. The big screens show main's pages until these are brought to 0.4.2. What follows describes
 * the October build. */
/* render_wide.c — the KEEPER (milestones), SETTINGS, UPGRADES (shop) and
 * VISITORS (collection) pages for the HOME TANK's wide glass (TANK_P4,
 * 1280 x 800, and 1280 x 720 on the Tab5; docs/home-tank-pages.md, 2026-10-12 / 13).
 *
 * Included from render.c inside #ifdef TANK_P4 so it shares the renderer's
 * primitives, the badge tables and the page state; the small pages' bodies
 * sit under #ifndef TANK_P4 there. Same function names and signatures, so
 * main.c, the touch port, the director and the selftests do not change:
 * the page-asks-where functions answer in this layout.
 *
 * The frame: a RAIL of tall buttons down the left (KEEPER, UPGRADES,
 * VISITORS, SETTINGS; CLOSE apart at the foot) and the content to its
 * right. KEEPER is a LIST pane (one row per swimmer, then NEW FRY, then
 * TANK) and a DETAIL pane that is always open for the selected row: what
 * the small page's modal showed, plus every badge with its words - the
 * list IS the arrows. SETTINGS is two columns of the same rows with a note
 * under each. Everything is drawn in FRAME coordinates (ctx_full); the fish
 * helpers take PAGE coordinates, so they get frame - PAGE_X / PAGE_Y. */

/* ---- the frame ---- */
#define WR_X       32                 /* the rail */
#define WR_Y0      32
#define WR_W       200
#define WR_H       64
#define WR_DY      72
#define WR_CLOSE_Y (TANK_H - 96)       /* 704 on the 10.1, 624 on the Tab5 */
#define WR_N       4                  /* KEEPER, UPGRADES, VISITORS, SETTINGS */
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
#define WD_LIST_Y  (WD_BTN_Y + WD_BTN_H + 2 * WD_DIV_GAP)   /* a fish's badges, listed: under the divider that follows the buttons */
#ifdef TANK_TAB5
#define WD_LIST_DY 52                 /* six badges from y 370 end at 682 */
#else
#define WD_LIST_DY 62
#endif
#define WD_GRID_X0 (WD_X + 40)        /* the tank's badges, a grid */
#define WD_GRID_Y  (WC_Y + 226 + WD_DIV_GAP + 8)   /* ... under its divider (a picked badge's box stands 8 px proud) */
#define WD_GATE_DY 56                 /* the fry pane's gates */
#define WL_TANK_GAP 24                /* the list: the TANK row stands apart from the fish above it, the divider in the middle */
#define WD_GRID_DX 64
#define WD_GRID_DY 56
#define WD_GRID_COLS 6
/* the rail's words, in order */
enum { WR_KEEPER = 0, WR_UPGRADES, WR_VISITORS, WR_SETTINGS };
static const char *const WR_WORDS[WR_N] = { "KEEPER", "UPGRADES", "VISITORS", "SETTINGS" };
#define W_BTN_FILL 0x1c2f36

/* ---- the KEEPER page's state ---- */
enum { WS_NONE = 0, WS_FISH, WS_FRY, WS_TANK };
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
    if (n < max) { out[n].kind = WS_TANK; out[n].slot = -1; n++; }
    return n;
}
#define W_ROWS_MAX (N_FISH_SLOTS + 2)
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
    rect_fill(c, x, WC_Y, w, WC_H, 0x071a21);
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

/* ---- the KEEPER page ---- */
void render_milestones(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    g_dirty_hold = true;
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    wrow_t rows[W_ROWS_MAX]; int n = w_rows(t, rows, W_ROWS_MAX, nreq);
    w_settle(t, rows, n);
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
            fish_as_is(fb, stride, WL_FISH_X - PAGE_X, top + WL_ROW_H / 2 - PAGE_Y, f->size, f, f->color, f->fin, f->accent, FISH_ROOM_ROW, true, t->clock);
            draw_text(&c, WL_NAME_X, top + 6, 3, 0xffffff, f->name);
            if (vis) {
                int k = rows[r].slot - N_FISH_MAX;
                draw_text(&c, WL_NAME_X, top + 32, 2, 0xe8c872, "VISITING");
                if (t->vis_badge[k] >= 0) link_badge_draw(&c, WL_BADGE_X0 + 16, top + WL_ROW_H / 2, 12, t->vis_badge[k], 255);
            } else {
                draw_text(&c, WL_NAME_X, top + 32, 2, 0x5f8a92, STAGE_WORDS[f->stage & 3]);
                const int sx = WL_NAME_X + 104;                       /* past JUVENILE, the longest stage word */
                static const int GX[4] = { 0, 15, 33, 55 };
                for (int s = 0; s < 4; s++) fish_glyph(&c, sx + GX[s] + 8, top + 39, 2.2f + s * 0.9f, s <= (int)f->stage ? f->accent : MSP_DIM);
                for (int k = 0; k < 6; k++) {
                    uint32_t bit = FISH_BADGES[k].bit;
                    badge(&c, WL_BADGE_X0 + k * WL_BADGE_DX, top + (WL_ROW_H - MSP_ICON) / 2, FISH_BADGES[k].icon, (f->ms_bits & bit) != 0, (f->ms_seen & bit) == 0);
                }
            }
        } else if (rows[r].kind == WS_FRY) {
            uint32_t fry_rgb = staged ? MSP_TEAL : MSP_DIM;
            render_fish_preview(fb, stride, WL_FISH_X - PAGE_X, top + WL_ROW_H / 2 - PAGE_Y, 0.55f, fry_rgb, fry_rgb, fry_rgb, NULL, FISH_ROOM_ROW, t->clock);
            draw_text(&c, WL_NAME_X, top + 6, 3, MSP_TEAL, "NEW FRY");
            for (int k = 0; k < nreq; k++) rect_fill(&c, WL_NAME_X + k * 16, top + 36, 10, 6, req[k].met ? MSP_TEAL : MSP_DIM);
            for (int k = 0; k < nreq; k++) {
                int x = WL_BADGE_X0 + k * WL_BADGE_DX, y = top + (WL_ROW_H - MSP_ICON) / 2 - 2;
                w_fry_badge(&c, fb, stride, t, x, y, &req[k]);
                if (!req[k].met) {
                    int w = (int)(MSP_ICON * req[k].frac + 0.5f);
                    rect_fill(&c, x, y + MSP_ICON + 4, MSP_ICON, 3, MSP_DIM);
                    if (w > 0) rect_fill(&c, x, y + MSP_ICON + 4, w, 3, MSP_TEAL);
                }
            }
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
        fish_as_is(fb, stride, CX - PAGE_X, WC_Y + 78 - PAGE_Y, f->size * 1.6f, f, f->color, f->fin, f->accent, FISH_ROOM_CARD, false, t->clock);
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
            draw_text(&c, CX - text_w(STAGE_WORDS[f->stage & 3], 3) / 2, WC_Y + 172, 3, MSP_TEAL, STAGE_WORDS[f->stage & 3]);
            char form[40]; snprintf(form, sizeof form, "%s  %s  %s", SHAPE_NAMES[f->shape % SHAPE_N], FINS_NAMES[f->fins % FINS_N], PATTERN_NAMES[f->pattern % PATTERN_N]);
            draw_text(&c, CX - text_w(form, 2) / 2, WC_Y + 200, 2, 0x5f8a92, form);
            /* what it would fetch, or why it stays (the small card's line) */
            bool can = progression_fish_sellable(t, g_w_slot);
            int worth = progression_fish_value(t, g_w_slot);
            if (!can) g_ms_sell_armed = false; else g_ms_sell_asked = false;
            char sub[40] = "";
            if (g_ms_sell_armed) snprintf(sub, sizeof sub, "TAP AGAIN TO SELL");
            else if (can) snprintf(sub, sizeof sub, "WORTH %d SAND DOLLARS", worth);
            else if (g_ms_sell_asked) snprintf(sub, sizeof sub, t->n_fish <= FISH_KEEP_MIN ? "KEEP AT LEAST TWO FISH" : "NAME THE NEW FRY FIRST");
            if (sub[0]) draw_text(&c, CX - text_w(sub, 2) / 2, WC_Y + 226, 2, 0xffffff, sub);
            const int by = WD_BTN_Y, bx0 = CX - WD_BTN_W - WD_BTN_GAP / 2, bx1 = CX + WD_BTN_GAP / 2;
            button(&c, bx0, by, WD_BTN_W, WD_BTN_H, W_BTN_FILL, MSP_TEAL, "RENAME", 3);
            if (!can) { rect_fill(&c, bx1, by, WD_BTN_W, WD_BTN_H, 0x04141a); rect_edge(&c, bx1, by, WD_BTN_W, WD_BTN_H, MSP_DIM);
                        draw_text(&c, bx1 + (WD_BTN_W - text_w("SELL", 3)) / 2, by + (WD_BTN_H - 21) / 2, 3, MSP_DIM, "SELL"); }
            else if (g_ms_sell_armed) { char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", worth); button(&c, bx1, by, WD_BTN_W, WD_BTN_H, 0x155e58, 0x38dcc7, ok, 3); }
            else button(&c, bx1, by, WD_BTN_W, WD_BTN_H, W_BTN_FILL, MSP_TEAL, "SELL", 3);
            /* its six badges, each with its words: read all of them without a tap */
            for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(&c, x, WD_LIST_Y - WD_DIV_GAP, MSP_DIM, 200);
            for (int k = 0; k < 6; k++) {
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
}

int render_milestones_tap(const tank_t *t, float x, float y) {
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    wrow_t rows[W_ROWS_MAX]; int n = w_rows(t, rows, W_ROWS_MAX, nreq);
    w_settle(t, rows, n);
    int rh = w_rail_hit(x, y);
    if (rh == WR_N) return MS_TAP_CLOSE;
    if (rh == WR_SETTINGS) return MS_TAP_SETTINGS;
    if (rh == WR_UPGRADES) return MS_TAP_SHOP;
    if (rh == WR_VISITORS) return collection_count() > 0 ? MS_TAP_VISITORS : MS_TAP_KEPT;
    if (rh == WR_KEEPER) return MS_TAP_KEPT;
    if (x >= WL_X && x < WL_X + WL_W) {                              /* the list: a row selects; its badge column too */
        int r = w_row_at(rows, n, y);
        if (r < 0) return MS_TAP_NONE;
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
            if (y >= by - 12 && y < by + WD_BTN_H + 24) {
                if (x < cx) { g_w_kind = WS_NONE; return MS_TAP_RENAME + fish; }   /* the platform opens the wheel */
                if (!progression_fish_sellable(t, fish)) { g_ms_sell_asked = true; return MS_TAP_KEPT; }
                if (!g_ms_sell_armed) { g_ms_sell_armed = true; g_ms_sell_clock = t->clock; return MS_TAP_KEPT; }
                if (t->clock - g_ms_sell_clock < MSP_SELL_WAIT_S) return MS_TAP_KEPT;
                g_ms_sell_armed = false; g_w_kind = WS_NONE; return MS_TAP_SELL + fish;   /* the platform sells it; the pane settles on the first row */
            }
            g_ms_sell_armed = false;                                    /* a tap elsewhere disarms it */
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
void render_milestones_leave(void) { g_w_kind = WS_NONE; g_w_slot = -1; g_w_tank_k = -1; g_w_gate = -1; g_ms_sell_armed = false; g_ms_sell_asked = false; g_ms_tpage = 0; g_ms_fpage = 0; }
void render_milestones_show_fish(const tank_t *t, int fish) { if (fish >= 0 && fish < t->n_fish) w_select(WS_FISH, fish); }
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y) {
    if (g_w_kind != WS_FISH || g_w_slot < 0 || g_w_slot >= t->n_fish) return false;
    const int cx = WD_X + WD_W / 2;
    if (fish) *fish = g_w_slot;
    if (rename_x) *rename_x = cx - WD_BTN_GAP / 2 - WD_BTN_W / 2;
    if (sell_x)   *sell_x   = cx + WD_BTN_GAP / 2 + WD_BTN_W / 2;
    if (btn_y)    *btn_y    = WD_BTN_Y + WD_BTN_H / 2;
    return true;
}
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y) { (void)t; (void)right; (void)x; (void)y; return false; }
void render_milestones_row(int row, int *name_x, int *y) {
    if (name_x) *name_x = WL_NAME_X + 20;
    if (y)      *y      = WL_ROW_Y0 + row * WL_ROW_H + WL_ROW_H / 2;   /* (a fish's or the NEW FRY row: the TANK row, set apart, is MSW_COIN's) */
}
int render_ms_foot_arrow_x(void) { return WR_X + WR_W / 2; }      /* (the small page's foot arrow: here the VISITORS button) */
bool render_milestones_where(const tank_t *t, int what, int *x, int *y) {
    int cx = WR_X + WR_W / 2, cy = -1;
    switch (what) {
    case MSW_CLOSE:    cy = WR_CLOSE_Y + WR_H / 2; break;
    case MSW_SETTINGS: cy = WR_Y0 + WR_SETTINGS * WR_DY + WR_H / 2; break;
    case MSW_UPGRADES: cy = WR_Y0 + WR_UPGRADES * WR_DY + WR_H / 2; break;
    case MSW_VISITORS: cy = WR_Y0 + WR_VISITORS * WR_DY + WR_H / 2; break;
    case MSW_COIN: {                                           /* the TANK row's sand dollar */
        fry_req_t req[FRY_REQ_MAX]; bool staged;
        wrow_t rows[W_ROWS_MAX]; int n = w_rows(t, rows, W_ROWS_MAX, progression_next_fry(t, req, &staged));
        int r = w_row_of(rows, n, WS_TANK, -1); if (r < 0) return false;
        cx = WL_FISH_X; cy = w_row_top(rows, r) + 20; break; }
    default: return false;
    }
    if (x) *x = cx;
    if (y) *y = cy;
    return true;
}

/* ---- the SETTINGS page ---- */
#define WS_TITLE_Y  (WC_Y + 14)
#define WS_LX       300               /* the left column: SCREEN AND SOUND */
#define WS_RX       780               /* the right column: TANK */
#define WS_SEG_W    110
#define WS_SEG_H    56
#define WS_SEG_DX   120
#define WS_ARW_W    56
#define WS_SPAN_W   (2 * WS_SEG_DX + WS_SEG_W)   /* 350: three segments, or arrow + value + arrow */
#ifdef TANK_TAB5
#define WS_ROW_DY   132                /* the rows from 16 px higher, 8 closer: a note keeps 32 px of water above the next label */
#define WS_ROW1_DY  112
#else
#define WS_ROW_DY   140
#define WS_ROW1_DY  128
#endif
#define WS_L_ROW1   (WC_Y + WS_ROW1_DY)      /* BRIGHTNESS */
#define WS_L_ROW2   (WS_L_ROW1 + WS_ROW_DY)   /* VOLUME */
#define WS_L_ROW3   (WS_L_ROW2 + WS_ROW_DY)   /* LIGHTS OUT */
#define WS_R_ROW1   (WC_Y + WS_ROW1_DY)      /* AUTO FEED */
#define WS_R_ROW2   (WS_R_ROW1 + WS_ROW_DY + 40)   /* SCREEN (AUTO FEED has two note lines) */
#define WS_R_ROW3   (WS_R_ROW2 + WS_ROW_DY)   /* UPDATES */
#define WS_R_ROW4   (WS_R_ROW3 + 120)         /* LINK */
#define WS_BIG_W    230               /* UPDATES / LINK */
#define WS_SEG_Y(row) ((row) + 28)    /* the segments under the label */
enum { WS_HIT_NONE = 0, WS_HIT_CLOSE, WS_HIT_BRIGHT, WS_HIT_VOLUME, WS_HIT_LIGHT_PREV, WS_HIT_LIGHT_NEXT, WS_HIT_FEED, WS_HIT_SCREEN,
       WS_HIT_UPDATES, WS_HIT_LINK, WS_HIT_TANK, WS_HIT_SHOP, WS_HIT_VISITORS };
static void ws_row(ctx_t *c, int x, int row_y, const char *label, const char *const names[], int n, int chosen) {
    draw_text(c, x, row_y, 3, MSP_TEAL, label);
    for (int i = 0; i < n; i++) {
        int sx = x + i * WS_SEG_DX, sy = WS_SEG_Y(row_y);
        if (i == chosen) { button(c, sx, sy, WS_SEG_W, WS_SEG_H, MSP_TEAL, MSP_TEAL, "", 3);
                           draw_text(c, sx + (WS_SEG_W - text_w(names[i], 3)) / 2, sy + (WS_SEG_H - 21) / 2, 3, MSP_INK, names[i]); }
        else button(c, sx, sy, WS_SEG_W, WS_SEG_H, W_BTN_FILL, MSP_DIM, names[i], 3);
    }
}
static void ws_note(ctx_t *c, int x, int y, const char *s) { draw_text(c, x, y, 2, 0x5f8a92, s); }
static void ws_arrow(ctx_t *c, int x, int y, bool right, bool live) {
    uint32_t rgb = live ? MSP_TEAL : 0x2c4a52;
    button(c, x, y, WS_ARW_W, WS_SEG_H, W_BTN_FILL, rgb, "", 3);
    int cx = x + WS_ARW_W / 2, cy = y + WS_SEG_H / 2;
    for (int i = 0; i < 9; i++) { int px = right ? cx - 4 + i : cx + 4 - i, hh = 8 - i; rect_fill(c, px, cy - hh, 1, 2 * hh + 1, rgb); }
}
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct, int volume) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    g_dirty_hold = true;
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    w_rail(&c, WR_SETTINGS, collection_count() > 0);
    w_pane(&c, WC_X, WC_W, 0x2c4a52);
    draw_text(&c, WC_X + (WC_W - text_w("SETTINGS", 4)) / 2, WS_TITLE_Y, 4, 0xffffff, "SETTINGS");
    ws_note(&c, WS_LX, WC_Y + 84, "SCREEN AND SOUND");
    ws_note(&c, WS_RX, WC_Y + 84, "TANK");
    int bi = bright_pct <= 30 ? 0 : bright_pct <= 60 ? 1 : 2;
    ws_row(&c, WS_LX, WS_L_ROW1, "BRIGHTNESS", SET_BRIGHT, 3, bi);
    ws_note(&c, WS_LX, WS_L_ROW1 + 100, "THE BACKLIGHT. 100% FOR A BRIGHT ROOM.");
    ws_row(&c, WS_LX, WS_L_ROW2, "VOLUME", SET_VOLUME, 3, volume < 0 ? 0 : volume > 2 ? 2 : volume);
    ws_note(&c, WS_LX, WS_L_ROW2 + 100, "FISH ARE QUIET AT NIGHT");
    {   /* LIGHTS OUT: < the choice > */
        int ch = tank_light_choice(t), y = WS_SEG_Y(WS_L_ROW3);
        int bx = WS_LX + WS_ARW_W + 8, bw = WS_SPAN_W - 2 * (WS_ARW_W + 8);
        draw_text(&c, WS_LX, WS_L_ROW3, 3, MSP_TEAL, "LIGHTS OUT");
        ws_arrow(&c, WS_LX, y, false, ch > 0);
        button(&c, bx, y, bw, WS_SEG_H, MSP_TEAL, MSP_TEAL, "", 3);
        draw_text(&c, bx + (bw - text_w(SET_LIGHT[ch], 3)) / 2, y + (WS_SEG_H - 21) / 2, 3, MSP_INK, SET_LIGHT[ch]);
        ws_arrow(&c, WS_LX + WS_SPAN_W - WS_ARW_W, y, true, ch < LIGHT_IDLE_N);
        ws_note(&c, WS_LX, WS_L_ROW3 + 100, "THE GLASS GOES DARK, THE FISH KEEP");
        ws_note(&c, WS_LX, WS_L_ROW3 + 120, "SWIMMING. A TOUCH WAKES IT.");
    }
    ws_row(&c, WS_RX, WS_R_ROW1, "AUTO FEED", SET_FEED, 2, t->autofeed_off ? 1 : 0);
    ws_note(&c, WS_RX, WS_R_ROW1 + 100, "ON: FOOD FALLS WHEN THEY BEG.");
    ws_note(&c, WS_RX, WS_R_ROW1 + 120, "OFF: HUNGER COSTS TRUST.");
    {   /* SCREEN: the way up of a mounted panel (no IMU here: the keeper turns it once) */
        static const char *const SCR[2] = { "NORMAL", "TURNED" };
        ws_row(&c, WS_RX, WS_R_ROW2, "SCREEN", SCR, 2, t->orient_inv ? 1 : 0);
        ws_note(&c, WS_RX, WS_R_ROW2 + 100, "MOUNTED THE OTHER WAY AROUND?");
    }
    draw_text(&c, WS_RX, WS_R_ROW3, 3, MSP_TEAL, "UPDATES");
    button(&c, WS_RX, WS_SEG_Y(WS_R_ROW3), WS_BIG_W, WS_SEG_H, W_BTN_FILL, MSP_TEAL, "CHECK", 3);
    ws_note(&c, WS_RX + WS_BIG_W + 16, WS_SEG_Y(WS_R_ROW3) + 21, "NO RADIO YET");
    draw_text(&c, WS_RX, WS_R_ROW4, 3, MSP_TEAL, "LINK");
    button(&c, WS_RX, WS_SEG_Y(WS_R_ROW4), WS_BIG_W, WS_SEG_H, W_BTN_FILL, MSP_TEAL, "", 3);
    { int gw = 20 + 10 + text_w("LINK", 3), gx = WS_RX + (WS_BIG_W - gw) / 2;
      link_glyph(&c, gx, WS_SEG_Y(WS_R_ROW4) + (WS_SEG_H - 12) / 2, 0xffffff);
      draw_text(&c, gx + 30, WS_SEG_Y(WS_R_ROW4) + (WS_SEG_H - 21) / 2, 3, 0xffffff, "LINK"); }
    ws_note(&c, WS_RX + WS_BIG_W + 16, WS_SEG_Y(WS_R_ROW4) + 21, "NO RADIO YET");
    char ver[64]; snprintf(ver, sizeof ver, "V%s %s  BUILD %s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string());
    draw_text_8px(&c, WS_LX, WC_Y + WC_H - 20, MSP_DIM, ver);
}
static int ws_segment(float x, int x0, int n) {
    if (x < x0 - 10) return -1;
    int i = (int)((x - x0 + 5) / WS_SEG_DX);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
/* the bare hit test: WS_HIT_* with *value as the small page's render_settings_tap gives it */
static int ws_hit(float x, float y, int *value) {
    *value = 0;
    int rh = w_rail_hit(x, y);
    if (rh == WR_N) return WS_HIT_TANK;
    if (rh == WR_KEEPER) return WS_HIT_CLOSE;
    if (rh == WR_UPGRADES) return WS_HIT_SHOP;
    if (rh == WR_VISITORS) return WS_HIT_VISITORS;
    if (rh == WR_SETTINGS) return WS_HIT_NONE;
    if (x >= WS_LX - 10 && x < WS_RX - 40) {                         /* the left column: a band from a little above each row to the next */
        if (y >= WS_SEG_Y(WS_L_ROW1) - 20 && y < WS_SEG_Y(WS_L_ROW2) - 20) { int s = ws_segment(x, WS_LX, 3); if (s < 0) return WS_HIT_NONE; *value = SET_BRIGHT_PCT[s]; return WS_HIT_BRIGHT; }
        if (y >= WS_SEG_Y(WS_L_ROW2) - 20 && y < WS_SEG_Y(WS_L_ROW3) - 20) { int s = ws_segment(x, WS_LX, 3); if (s < 0) return WS_HIT_NONE; *value = s; return WS_HIT_VOLUME; }
        if (y >= WS_SEG_Y(WS_L_ROW3) - 20 && y < WS_SEG_Y(WS_L_ROW3) + WS_SEG_H + 40) {
            if (x < WS_LX - 10 || x >= WS_LX + WS_SPAN_W + 10) return WS_HIT_NONE;
            return x < WS_LX + WS_SPAN_W / 2 ? WS_HIT_LIGHT_PREV : WS_HIT_LIGHT_NEXT;
        }
        return WS_HIT_NONE;
    }
    if (x >= WS_RX - 10) {
        if (y >= WS_SEG_Y(WS_R_ROW1) - 20 && y < WS_SEG_Y(WS_R_ROW2) - 20) { int s = ws_segment(x, WS_RX, 2); if (s < 0) return WS_HIT_NONE; *value = s == 0; return WS_HIT_FEED; }
        if (y >= WS_SEG_Y(WS_R_ROW2) - 20 && y < WS_SEG_Y(WS_R_ROW3) - 20) { int s = ws_segment(x, WS_RX, 2); if (s < 0) return WS_HIT_NONE; *value = s == 1; return WS_HIT_SCREEN; }
        if (y >= WS_SEG_Y(WS_R_ROW3) - 20 && y < WS_SEG_Y(WS_R_ROW4) - 20) return x < WS_RX + WS_BIG_W + 20 ? WS_HIT_UPDATES : WS_HIT_NONE;
        if (y >= WS_SEG_Y(WS_R_ROW4) - 20 && y < WS_SEG_Y(WS_R_ROW4) + WS_SEG_H + 40) return x < WS_RX + WS_BIG_W + 20 ? WS_HIT_LINK : WS_HIT_NONE;
    }
    return WS_HIT_NONE;
}
int render_settings_tap(float x, float y, int *value) {
    int v = 0, h = ws_hit(x, y, &v); *value = v;
    switch (h) {
    case WS_HIT_CLOSE:    return SET_TAP_CLOSE;
    case WS_HIT_BRIGHT:   return SET_TAP_BRIGHT;
    case WS_HIT_VOLUME:   return SET_TAP_VOLUME;
    case WS_HIT_LIGHT_PREV: case WS_HIT_LIGHT_NEXT: return SET_TAP_LIGHT;
    case WS_HIT_FEED:     return SET_TAP_FEED;
    case WS_HIT_SCREEN:   return SET_TAP_SCREEN;
    case WS_HIT_UPDATES:  return SET_TAP_UPDATES;
    case WS_HIT_LINK:     return SET_TAP_LINK;
    case WS_HIT_TANK:     return SET_TAP_TANK;
    case WS_HIT_SHOP:     return SET_TAP_SHOP;
    case WS_HIT_VISITORS: return SET_TAP_VISITORS;
    default:              return SET_TAP_NONE;
    }
}
int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    static bool s_down; static float s_px, s_py; static int s_hit, s_hv;
    int r = SET_TAP_NONE; *value = 0;
    if (down && !s_down) { s_px = x; s_py = y; s_hit = ws_hit(x, y, &s_hv); }
    else if (!down && s_down) {
        int v = 0, h = ws_hit(x, y, &v);
        float dx = x - s_px, dy = y - s_py;
        if (h == s_hit && dx * dx + dy * dy < 30 * 30) {
            if (h == WS_HIT_LIGHT_PREV || h == WS_HIT_LIGHT_NEXT) {
                int was = tank_light_choice(t), now = was + (h == WS_HIT_LIGHT_NEXT ? 1 : -1);
                if (now >= 0 && now <= LIGHT_IDLE_N) {
                    tank_light_choice_set(t, now); tank_emit(TEV_WHEEL_TICK, -1); progression_settings_changed();
                    if ((was == 0) != (now == 0)) { r = SET_TAP_LIGHT; *value = now != 0; }
                    else { r = SET_TAP_IDLE; *value = t->light_idle_s; }
                }
            } else if (h == WS_HIT_FEED) {
                if ((v == 0) != t->autofeed_off) { t->autofeed_off = v == 0; progression_settings_changed(); }
                r = SET_TAP_FEED; *value = v;
            } else if (h == WS_HIT_SCREEN) {                         /* NORMAL / TURNED: the way up, locked (no IMU to follow) */
                if ((v != 0) != t->orient_inv) { tank_orient_lock(t, false); tank_orient(t, v != 0); tank_orient_lock(t, true); progression_settings_changed(); }
                r = SET_TAP_SCREEN; *value = v;
            } else if (h != WS_HIT_NONE) { r = render_settings_tap(x, y, &v); *value = v; }
        }
    }
    s_down = down;
    return r;
}
bool render_settings_where(int what, int seg, int *x, int *y) {
    int cx = -1, cy = -1;
    switch (what) {
    case SET_TAP_BRIGHT: cx = WS_LX + seg * WS_SEG_DX + WS_SEG_W / 2; cy = WS_SEG_Y(WS_L_ROW1) + WS_SEG_H / 2; break;
    case SET_TAP_VOLUME: cx = WS_LX + seg * WS_SEG_DX + WS_SEG_W / 2; cy = WS_SEG_Y(WS_L_ROW2) + WS_SEG_H / 2; break;
    case SET_TAP_LIGHT: case SET_TAP_IDLE:                      /* seg 0 = the previous arrow, 1 = the next */
        cx = seg ? WS_LX + WS_SPAN_W - WS_ARW_W / 2 : WS_LX + WS_ARW_W / 2; cy = WS_SEG_Y(WS_L_ROW3) + WS_SEG_H / 2; break;
    case SET_TAP_FEED:   cx = WS_RX + seg * WS_SEG_DX + WS_SEG_W / 2; cy = WS_SEG_Y(WS_R_ROW1) + WS_SEG_H / 2; break;
    case SET_TAP_SCREEN: cx = WS_RX + seg * WS_SEG_DX + WS_SEG_W / 2; cy = WS_SEG_Y(WS_R_ROW2) + WS_SEG_H / 2; break;
    case SET_TAP_UPDATES: cx = WS_RX + WS_BIG_W / 2; cy = WS_SEG_Y(WS_R_ROW3) + WS_SEG_H / 2; break;
    case SET_TAP_LINK:    cx = WS_RX + WS_BIG_W / 2; cy = WS_SEG_Y(WS_R_ROW4) + WS_SEG_H / 2; break;
    case SET_TAP_CLOSE:   cx = WR_X + WR_W / 2; cy = WR_Y0 + WR_KEEPER * WR_DY + WR_H / 2; break;     /* KEEPER: back to the milestones page */
    case SET_TAP_TANK:    cx = WR_X + WR_W / 2; cy = WR_CLOSE_Y + WR_H / 2; break;
    case SET_TAP_SHOP:    cx = WR_X + WR_W / 2; cy = WR_Y0 + WR_UPGRADES * WR_DY + WR_H / 2; break;
    case SET_TAP_VISITORS: cx = WR_X + WR_W / 2; cy = WR_Y0 + WR_VISITORS * WR_DY + WR_H / 2; break;
    default: return false;                                      /* (no ROTATION here: SCREEN is the row) */
    }
    if (x) *x = cx;
    if (y) *y = cy;
    return true;
}

/* ---- the UPGRADES page (the shop), 2026-10-13 ----
 * The same frame. The list pane: the coin and the balance at its head, a row
 * per item (no pages: every row fits), HOW TO EARN at its foot. The detail
 * pane is always open for the row selected: the art at 4x, the name, the
 * words, the price and UNLOCK - or IN THE TANK with MOVE and SELL for a piece
 * that is placed - and HOW TO EARN's lines when that button is the one lit.
 * No modal: g_shp_modal is the item selected (SHP_MODAL_FRY: the fry), g_shp_earn
 * the earn pane.
 * THE FRY (0.3.5 on the small page; here 2026-10-21): once a fish has been sold
 * the list's first row is FRY, as on the small page (shp_rows / shp_item), and
 * its pane holds the three side by side at a size to choose by - a tap picks
 * one, BUY buys it (SHOP_TAP_FRY + its place). The pumpkin's row is there only
 * while this build sells it, or the keeper owns one (progression_pumpkin_offered).
 * The rows are sized for the most there can be (FRY + every item), so the list
 * never changes its pitch when the fry come. */
#define WSH_ROW_Y0  (WC_Y + 100 + WD_DIV_GAP)
#define WSH_EARN_W  230
#define WSH_EARN_H  48
#define WSH_EARN_Y  (WC_Y + WC_H - 24 - WSH_EARN_H)
#define WSH_ROWS_MAX (SD_ITEM_COUNT + 1)                                  /* FRY + every item */
#define WSH_ROW_H   ((WSH_EARN_Y - 8 - WSH_ROW_Y0) / WSH_ROWS_MAX)         /* 53 on the 10.1 (59 with nine), 45 on the Tab5 (50) */
#define WSH_CHIP_W  116
#define WSH_CHIP_H  36
#define WSH_BTN_Y   (WC_Y + 400)
#define WSH_BTN_H   56
#define WSH_BUY_W   240
#define WSH_TWO_W   180
#define WSH_TWO_GAP 20
#define WSF_BOX_W   136                                                   /* the fry pane's three boxes */
#define WSF_BOX_H   176
#define WSF_BOX_GAP 16
#define WSF_BOX_X(k) (WD_X + (WD_W - 3 * WSF_BOX_W - 2 * WSF_BOX_GAP) / 2 + (k) * (WSF_BOX_W + WSF_BOX_GAP))
#define WSF_BOX_Y   (WC_Y + 112)
#define WSF_PRICE_Y (WSF_BOX_Y + WSF_BOX_H + 2 * WD_DIV_GAP + 8)
#define WSF_BUY_Y   (WSF_PRICE_Y + 84)
_Static_assert(WSH_ROW_H >= 44, "the shop's rows are too short for a name over a price: page them, or move HOW TO EARN");
_Static_assert(WSF_BUY_Y + WSH_BTN_H + 60 <= WC_Y + WC_H, "the fry pane runs off the glass");
static bool wsh_row_shows(const tank_t *t, int item) { return item != 8 || progression_pumpkin_offered(t); }   /* (the pumpkin: the last item) */
static int  wsh_row_of(const tank_t *t, int item) {                       /* an item's row, -1 = none; item -1 = FRY */
    if (item < 0) return shp_fry(t) ? 0 : -1;
    return item < SD_ITEM_COUNT && wsh_row_shows(t, item) ? item + shp_fry(t) : -1;
}
static void wsh_settle(const tank_t *t) {
    if (g_shp_earn) return;
    bool ok = g_shp_modal == SHP_MODAL_FRY ? shp_fry(t) : g_shp_modal >= 0 && wsh_row_of(t, g_shp_modal) >= 0;   /* (-1, the page just opened, is no item:
                                                                                  wsh_row_of reads -1 as FRY - the 10.1 read SD_ITEMS[-1], 10-21) */
    if (!ok) { g_shp_modal = shp_fry(t) ? SHP_MODAL_FRY : 0; g_shp_fry_pick = -1; }   /* the top row lit */
    if (g_shp_modal == SHP_MODAL_FRY) {                                   /* a pick still for sale (the first left, else none lit) */
        bool gone = g_shp_fry_pick < 0 || g_shp_fry_pick >= FRY_OFFER_N;
        if (!gone) { fry_offer_t o; progression_fry_offer(g_shp_fry_pick, &o); gone = o.sold; }
        if (gone) { int k = fry_first_left(); g_shp_fry_pick = k < 0 ? 0 : k; }
    }
}
static void w_lit_button(ctx_t *c, int x, int y, int w, int h, const char *label, int scale) {   /* teal, ink lettering */
    button(c, x, y, w, h, MSP_TEAL, MSP_TEAL, "", scale);
    draw_text(c, x + (w - text_w(label, scale)) / 2, y + (h - 7 * scale) / 2, scale, MSP_INK, label);
}
/* a row's two lines and its chip, centred in WSH_ROW_H */
#define WSH_TXT_Y(top) ((top) + (WSH_ROW_H - 2 - 40) / 2)
#define WSH_ICON_Y(top) ((top) + (WSH_ROW_H - 2 - 32) / 2)
static void wsh_chip(ctx_t *c, int top, int how, const char *label) {     /* how: 0 dim, 1 lit, 2 ink (IN TANK) */
    const int bx = WL_X + WL_W - 24 - WSH_CHIP_W, by = top + (WSH_ROW_H - 2 - WSH_CHIP_H) / 2;
    if (how == 1) w_lit_button(c, bx, by, WSH_CHIP_W, WSH_CHIP_H, label, 2);
    else button(c, bx, by, WSH_CHIP_W, WSH_CHIP_H, how == 2 ? MSP_INK : W_BTN_FILL, MSP_DIM, label, 2);
}
static void wsh_fry_row(ctx_t *c, const tank_t *t, uint16_t *fb, int stride, int top) {
    int k = fry_first_left();
    bool room = progression_fry_room_to_buy(t), can = t->sd_balance >= SD_PRICE_FRY;
    if (k >= 0) { fry_offer_t o; progression_fry_offer(k, &o); fry_draw(fb, stride, WL_X + 48 - PAGE_X, top + (WSH_ROW_H - 2) / 2 - PAGE_Y, 1.1f, &o, t->clock); }
    const int ty = WSH_TXT_Y(top);
    draw_text(c, WL_X + 84, ty, 3, 0xffffff, "FRY");
    if (k < 0) { char l[24]; fry_hours(l, sizeof l); draw_text(c, WL_X + 84, ty + 26, 2, MSP_DIM, l); }
    else price_tag(c, WL_X + 84, ty + 26, SD_PRICE_FRY, can && room ? MSP_TEAL : MSP_DIM);
    wsh_chip(c, top, k >= 0 && room && can, k < 0 ? "SOLD OUT" : !room ? "FULL" : "CHOOSE");
}
static void wsh_fry_pane(ctx_t *c, const tank_t *t, uint16_t *fb, int stride) {
    const int CX = WD_X + WD_W / 2;
    draw_text(c, CX - text_w("FRY FOR SALE", 4) / 2, WC_Y + 36, 4, 0xffffff, "FRY FOR SALE");
    draw_text(c, CX - text_w("PICK ONE TO RAISE", 2) / 2, WC_Y + 76, 2, MSP_TEAL, "PICK ONE TO RAISE");
    for (int k = 0; k < FRY_OFFER_N; k++) {
        fry_offer_t o; progression_fry_offer(k, &o);
        int bx = WSF_BOX_X(k), by = WSF_BOX_Y;
        bool lit = !o.sold && k == g_shp_fry_pick;
        rect_fill(c, bx, by, WSF_BOX_W, WSF_BOX_H, lit ? 0x14303a : 0x0b1d23);
        uint32_t e = lit ? 0xffffff : o.sold ? 0x1c2f36 : 0x4d6a72;
        rect_edge(c, bx, by, WSF_BOX_W, WSF_BOX_H, e);
        if (lit) { rect_edge(c, bx + 1, by + 1, WSF_BOX_W - 2, WSF_BOX_H - 2, e); rect_edge(c, bx + 2, by + 2, WSF_BOX_W - 4, WSF_BOX_H - 4, e); }
        if (o.sold) { draw_text(c, bx + (WSF_BOX_W - text_w("BOUGHT", 2)) / 2, by + WSF_BOX_H / 2 - 7, 2, 0x2c4a52, "BOUGHT"); continue; }
        fry_draw(fb, stride, bx + WSF_BOX_W / 2 - PAGE_X, by + 58 - PAGE_Y, 1.7f, &o, t->clock + k * 0.7f);
        /* its colors, both: the body and the markings it grows into - each swatch with its word under it */
        const int sy = by + WSF_BOX_H - 62, c0 = bx + 34, c1 = bx + WSF_BOX_W - 34;
        rect_fill(c, c0 - 16, sy, 32, 22, o.body);   rect_edge(c, c0 - 16, sy, 32, 22, 0x4d6a72);
        rect_fill(c, c1 - 16, sy, 32, 22, o.accent); rect_edge(c, c1 - 16, sy, 32, 22, 0x4d6a72);
        draw_text(c, c0 - text_w("BODY", 2) / 2, sy + 32, 2, 0x7fa3b5, "BODY");
        draw_text(c, c1 - text_w("MARKS", 2) / 2, sy + 32, 2, 0x7fa3b5, "MARKS");
    }
    for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(c, x, WSF_BOX_Y + WSF_BOX_H + WD_DIV_GAP, MSP_DIM, 200);
    bool room = progression_fry_room_to_buy(t), can = t->sd_balance >= SD_PRICE_FRY;
    fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
    bool go = room && can && !pk.sold && progression_fry_left() > 0;
    char line[32]; snprintf(line, sizeof line, "%d", SD_PRICE_FRY);
    int pw = 40 + text_w(line, 4);
    blit_icon(c, CX - pw / 2, WSF_PRICE_Y - 2, &icon_ms_sand_dollar, 255);
    draw_text(c, CX - pw / 2 + 40, WSF_PRICE_Y, 4, go ? 0xffffff : MSP_DIM, line);
    if (progression_fry_left() == 0) snprintf(line, sizeof line, "ALL THREE BOUGHT");
    else if (!room) snprintf(line, sizeof line, t->n_fish >= POP_CAP ? "THE TANK IS FULL" : "WELCOME THE NEW FRY FIRST");
    else if (!can) snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
    else line[0] = 0;
    if (line[0]) draw_text(c, CX - text_w(line, 2) / 2, WSF_PRICE_Y + 46, 2, 0x5f8a92, line);
    if (go) w_lit_button(c, CX - WSH_BUY_W / 2, WSF_BUY_Y, WSH_BUY_W, WSH_BTN_H, "BUY", 3);
    else button(c, CX - WSH_BUY_W / 2, WSF_BUY_Y, WSH_BUY_W, WSH_BTN_H, W_BTN_FILL, MSP_DIM, "BUY", 3);
    fry_hours(line, sizeof line);
    draw_text(c, CX - text_w(line, 2) / 2, WSF_BUY_Y + WSH_BTN_H + 22, 2, 0x3f6a72, line);
}
void render_shop(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    g_dirty_hold = true;
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    wsh_settle(t);
    w_rail(&c, WR_UPGRADES, collection_count() > 0);
    /* the list */
    w_pane(&c, WL_X, WL_W, 0x2c4a52);
    blit_icon(&c, WL_X + 24, WC_Y + 16, &icon_shop_sand_dollar_64, 255);
    draw_text(&c, WL_X + 104, WC_Y + 22, 2, MSP_TEAL, "SAND DOLLARS");
    char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
    draw_text(&c, WL_X + 104, WC_Y + 44, 5, 0xffffff, bal);
    for (int x = WL_X + 24; x < WL_X + WL_W - 24; x++) px_blend(&c, x, WC_Y + 100, MSP_DIM, 200);
    for (int r = 0; r < shp_rows(t); r++) {
        int i = shp_item(t, r), top = WSH_ROW_Y0 + r * WSH_ROW_H;
        bool sel = !g_shp_earn && (i < 0 ? g_shp_modal == SHP_MODAL_FRY : i == g_shp_modal);
        if (sel) { rect_fill(&c, WL_X + 8, top, WL_W - 16, WSH_ROW_H - 2, W_BTN_FILL); rect_edge(&c, WL_X + 8, top, WL_W - 16, WSH_ROW_H - 2, MSP_TEAL); }
        if (i < 0) { wsh_fry_row(&c, t, fb, stride, top); continue; }
        const sd_item_t *it = &SD_ITEMS[i];
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
        if (owned) blit_icon(&c, WL_X + 32, WSH_ICON_Y(top), shop_icon(i), 255); else blit_icon_locked(&c, WL_X + 32, WSH_ICON_Y(top), shop_icon(i));
        const int ty = WSH_TXT_Y(top);
        draw_text(&c, WL_X + 84, ty, 3, 0xffffff, it->name);
        if (owned) draw_text(&c, WL_X + 84, ty + 26, 2, MSP_TEAL, "IN THE TANK");
        else price_tag(&c, WL_X + 84, ty + 26, it->price, can ? MSP_TEAL : MSP_DIM);
        wsh_chip(&c, top, owned ? 2 : can ? 1 : 0, owned ? "IN TANK" : "UNLOCK");
    }
    if (g_shp_earn) w_lit_button(&c, WL_X + 24, WSH_EARN_Y, WSH_EARN_W, WSH_EARN_H, "HOW TO EARN", 3);
    else button(&c, WL_X + 24, WSH_EARN_Y, WSH_EARN_W, WSH_EARN_H, W_BTN_FILL, MSP_TEAL, "HOW TO EARN", 3);
    /* the detail pane */
    w_pane(&c, WD_X, WD_W, MSP_TEAL);
    const int CX = WD_X + WD_W / 2;
    if (g_shp_earn) {
        draw_text(&c, CX - text_w("HOW TO EARN", 4) / 2, WC_Y + 40, 4, 0xffffff, "HOW TO EARN");
        blit_icon(&c, CX - 32, WC_Y + 92, &icon_shop_sand_dollar_64, 255);
        const char *const *lines = progression_sd_earn_lines();
        for (int i = 0; lines[i]; i++) draw_text(&c, WD_X + 40, WC_Y + 190 + i * 40, 2, MSP_TEAL, lines[i]);
        return;
    }
    if (g_shp_modal == SHP_MODAL_FRY) { wsh_fry_pane(&c, t, fb, stride); return; }
    const int item = g_shp_modal;
    if (item < 0 || item >= SD_ITEM_COUNT) return;            /* (wsh_settle keeps it an item: never a read past the table) */
    const sd_item_t *it = &SD_ITEMS[item];
    bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
    blit_icon_scaled(&c, CX - 64, WC_Y + 36, shop_icon(item), 4, true);
    draw_text(&c, CX - text_w(it->name, 4) / 2, WC_Y + 188, 4, 0xffffff, it->name);
    draw_text(&c, CX - text_w(it->words, 2) / 2, WC_Y + 236, 2, MSP_TEAL, it->words);
    draw_text(&c, CX - text_w(it->words2, 2) / 2, WC_Y + 258, 2, MSP_TEAL, it->words2);
    for (int x = WD_X + 24; x < WD_X + WD_W - 24; x++) px_blend(&c, x, WC_Y + 292, MSP_DIM, 200);
    if (owned && tank_decor_placeable(item)) {              /* a placed piece: MOVE re-opens its placement page, SELL (twice) sells it back */
        char sell[24]; snprintf(sell, sizeof sell, "SELLS BACK FOR %d", progression_sell_value(item));
        draw_text(&c, CX - text_w("IN THE TANK", 3) / 2, WC_Y + 316, 3, MSP_TEAL, "IN THE TANK");
        draw_text(&c, CX - text_w(sell, 2) / 2, WC_Y + 352, 2, 0x5f8a92, sell);
        const int bx0 = CX - WSH_TWO_W - WSH_TWO_GAP / 2, bx1 = CX + WSH_TWO_GAP / 2;
        button(&c, bx0, WSH_BTN_Y, WSH_TWO_W, WSH_BTN_H, W_BTN_FILL, MSP_TEAL, "MOVE", 3);
        if (g_shp_sell_armed) { snprintf(sell, sizeof sell, "+%d OK?", progression_sell_value(item)); w_lit_button(&c, bx1, WSH_BTN_Y, WSH_TWO_W, WSH_BTN_H, sell, 3); }
        else button(&c, bx1, WSH_BTN_Y, WSH_TWO_W, WSH_BTN_H, W_BTN_FILL, MSP_DIM, "SELL", 3);
    } else if (owned) draw_text(&c, CX - text_w("IN THE TANK", 3) / 2, WC_Y + 316, 3, MSP_TEAL, "IN THE TANK");   /* a resident: there to stay */
    else {
        char line[32]; snprintf(line, sizeof line, "%d", it->price);
        int pw = 40 + text_w(line, 4);
        blit_icon(&c, CX - pw / 2, WC_Y + 312, &icon_ms_sand_dollar, 255);
        draw_text(&c, CX - pw / 2 + 40, WC_Y + 314, 4, can ? 0xffffff : MSP_DIM, line);
        if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
                    draw_text(&c, CX - text_w(line, 2) / 2, WC_Y + 360, 2, 0x5f8a92, line); }
        if (can) w_lit_button(&c, CX - WSH_BUY_W / 2, WSH_BTN_Y, WSH_BUY_W, WSH_BTN_H, "UNLOCK", 3);
        else button(&c, CX - WSH_BUY_W / 2, WSH_BTN_Y, WSH_BUY_W, WSH_BTN_H, W_BTN_FILL, MSP_DIM, "UNLOCK", 3);
    }
}
int render_shop_tap(const tank_t *t, float x, float y) {
    wsh_settle(t);
    int rh = w_rail_hit(x, y);
    if (rh == WR_N) return SHOP_TAP_TANK;
    if (rh == WR_KEEPER) return SHOP_TAP_CLOSE;                  /* back to the milestones page, as the small page's CLOSE */
    if (rh == WR_SETTINGS) return SHOP_TAP_SETTINGS;
    if (rh == WR_VISITORS) return collection_count() > 0 ? SHOP_TAP_VISITORS : SHOP_TAP_KEPT;
    if (rh == WR_UPGRADES) return SHOP_TAP_KEPT;
    if (x >= WL_X && x < WL_X + WL_W) {                          /* the list: a row selects; HOW TO EARN at the foot */
        if (y >= WSH_EARN_Y - 12) { if (x < WL_X + 24 + WSH_EARN_W + 24) { g_shp_earn = true; g_shp_sell_armed = false; return SHOP_TAP_KEPT; } return SHOP_TAP_NONE; }
        int r = (int)((y - WSH_ROW_Y0 + 4) / WSH_ROW_H);
        if (y < WSH_ROW_Y0 - 4 || r < 0 || r >= shp_rows(t)) return SHOP_TAP_NONE;
        int i = shp_item(t, r), m = i < 0 ? SHP_MODAL_FRY : i;
        if (m != g_shp_modal || g_shp_earn) g_shp_sell_armed = false;
        if (m == SHP_MODAL_FRY && g_shp_modal != SHP_MODAL_FRY) { int k = fry_first_left(); g_shp_fry_pick = k < 0 ? 0 : k; }   /* the first still for sale, lit */
        g_shp_modal = m; g_shp_earn = false;
        return SHOP_TAP_KEPT;
    }
    if (x >= WD_X && x < WD_X + WD_W) {                          /* the pane: the selected item's buttons */
        if (g_shp_earn) return SHOP_TAP_KEPT;
        if (g_shp_modal == SHP_MODAL_FRY) {                      /* the fry: a box picks, BUY buys the pick */
            const int cx = WD_X + WD_W / 2;
            for (int k = 0; k < FRY_OFFER_N; k++)
                if (x >= WSF_BOX_X(k) && x < WSF_BOX_X(k) + WSF_BOX_W && y >= WSF_BOX_Y && y < WSF_BOX_Y + WSF_BOX_H) {
                    fry_offer_t o; progression_fry_offer(k, &o);
                    if (!o.sold) g_shp_fry_pick = k;
                    return SHOP_TAP_KEPT;
                }
            fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
            bool go = progression_fry_room_to_buy(t) && t->sd_balance >= SD_PRICE_FRY && !pk.sold && progression_fry_left() > 0;
            if (go && y >= WSF_BUY_Y - 14 && y < WSF_BUY_Y + WSH_BTN_H + 14 && x >= cx - WSH_BUY_W / 2 - 30 && x < cx + WSH_BUY_W / 2 + 30) return SHOP_TAP_FRY + g_shp_fry_pick;
            return SHOP_TAP_KEPT;
        }
        const int item = g_shp_modal, cx = WD_X + WD_W / 2;
        const sd_item_t *it = &SD_ITEMS[item];
        bool owned = (t->sd_unlocks & it->bit) != 0, can = t->sd_balance >= it->price;
        bool row = y >= WSH_BTN_Y - 14 && y < WSH_BTN_Y + WSH_BTN_H + 28;
        if (owned && tank_decor_placeable(item)) {
            bool on_sell = row && x >= cx, on_move = row && x < cx;
            if (on_sell && !g_shp_sell_armed) { g_shp_sell_armed = true; return SHOP_TAP_KEPT; }
            g_shp_sell_armed = false;
            if (on_sell) return SHOP_TAP_SELL + item;
            if (on_move) return SHOP_TAP_MOVE + item;
            return SHOP_TAP_KEPT;
        }
        g_shp_sell_armed = false;
        if (row && !owned && can && x >= cx - WSH_BUY_W / 2 - 30 && x < cx + WSH_BUY_W / 2 + 30) return SHOP_TAP_BUY + item;
        return SHOP_TAP_KEPT;
    }
    return SHOP_TAP_NONE;
}
bool render_shop_where(const tank_t *t, int what, int item, int *x, int *y) {
    const int pcx = WD_X + WD_W / 2;
    int cx = -1, cy = WSH_BTN_Y + WSH_BTN_H / 2;
    switch (what) {
    case SHW_ROW:   { int r = wsh_row_of(t, item); if (r < 0) return false;      /* item -1: the FRY row */
                      cx = WL_X + 120; cy = WSH_ROW_Y0 + r * WSH_ROW_H + WSH_ROW_H / 2; } break;
    case SHW_BUY:   cx = pcx; if (g_shp_modal == SHP_MODAL_FRY) cy = WSF_BUY_Y + WSH_BTN_H / 2; break;
    case SHW_FRY:   if (item < 0 || item >= FRY_OFFER_N) return false;            /* a fry's box (its pane open) */
                    cx = WSF_BOX_X(item) + WSF_BOX_W / 2; cy = WSF_BOX_Y + WSF_BOX_H / 2; break;
    case SHW_MOVE:  cx = pcx - WSH_TWO_GAP / 2 - WSH_TWO_W / 2; break;
    case SHW_SELL:  cx = pcx + WSH_TWO_GAP / 2 + WSH_TWO_W / 2; break;
    case SHW_EARN:  cx = WL_X + 24 + WSH_EARN_W / 2; cy = WSH_EARN_Y + WSH_EARN_H / 2; break;
    case SHW_OFF:   cx = pcx; cy = WC_Y + 120; break;            /* on the art: no button there */
    case SHW_CLOSE: cx = WR_X + WR_W / 2; cy = WR_Y0 + WR_KEEPER * WR_DY + WR_H / 2; break;   /* KEEPER: back to the milestones page */
    default: return false;                                       /* (no page arrows: every row shows) */
    }
    if (x) *x = cx;
    if (y) *y = cy;
    return true;
}

/* ---- the VISITORS page (the collection), 2026-10-13 ----
 * The frame again: the grid of friends made in the list pane (three across,
 * twelve places), and the passport of the one selected - large - in the
 * detail pane, always open (the first, until another is tapped). */
#define WV_COLS 3
#define WV_CW   152
#ifdef TANK_TAB5
#define WV_CH   140
#else
#define WV_CH   146
#endif
#define WV_X0   (WL_X + (WL_W - WV_COLS * WV_CW) / 2)
#define WV_Y0   (WC_Y + 84)
_Static_assert(WV_Y0 + (COLL_MAX + WV_COLS - 1) / WV_COLS * WV_CH <= WC_Y + WC_H, "the collection's grid runs off the pane");
static void wv_field(ctx_t *c, int x, int y, const char *lbl, const char *val) {
    draw_text(c, x, y, 2, 0x7fa3b5, lbl);
    draw_text(c, x, y + 20, 3, 0xffffff, val);
}
void render_collection(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_full(fb, stride, 1.0f);
    g_dirty_hold = true;
    rect_fill(&c, 0, 0, TANK_W, TANK_H, MSP_INK);
    const uint32_t GOLD = 0xe8c872, INK = 0x13324f;
    int n = collection_count();
    if (g_coll_sel < 0 || g_coll_sel >= n) g_coll_sel = n > 0 ? 0 : -1;
    w_rail(&c, WR_VISITORS, true);
    w_pane(&c, WL_X, WL_W, 0x2c4a52);
    char line[40];
    draw_text(&c, WL_X + (WL_W - text_w("VISITOR COLLECTION", 3)) / 2, WC_Y + 18, 3, GOLD, "VISITOR COLLECTION");
    snprintf(line, sizeof line, "%d OF %d COLLECTED", n, COLL_MAX);
    draw_text(&c, WL_X + (WL_W - text_w(line, 2)) / 2, WC_Y + 50, 2, MSP_TEAL, line);
    for (int i = 0; i < COLL_MAX; i++) {
        int cx = WV_X0 + (i % WV_COLS) * WV_CW, cy = WV_Y0 + (i / WV_COLS) * WV_CH;
        if (i >= n) { rect_edge(&c, cx + 8, cy + 6, WV_CW - 16, WV_CH - 12, W_BTN_FILL); continue; }   /* a place to fill */
        const coll_entry_t *e = collection_get(i);
        if (i == g_coll_sel) { rect_fill(&c, cx + 8, cy + 6, WV_CW - 16, WV_CH - 12, W_BTN_FILL); rect_edge(&c, cx + 8, cy + 6, WV_CW - 16, WV_CH - 12, MSP_TEAL); rect_edge(&c, cx + 9, cy + 7, WV_CW - 18, WV_CH - 14, MSP_TEAL); }
        else rect_edge(&c, cx + 8, cy + 6, WV_CW - 16, WV_CH - 12, 0x3f6a72);
        fish_t f; entry_fish(&f, e);
        render_fish_portrait(fb, stride, cx + WV_CW / 2.0f - PAGE_X, cy + 58 - PAGE_Y, 1.2f, &f, WV_CW - 40, t->clock + i * 0.7f);
        draw_text(&c, cx + (WV_CW - text_w(f.name, 2)) / 2, cy + 106, 2, 0xffffff, f.name);
    }
    /* the passport */
    w_pane(&c, WD_X, WD_W, GOLD);
    const int X = WD_X + 24, W = WD_W - 48, Y = WC_Y + 24, CX = WD_X + WD_W / 2;
    if (g_coll_sel < 0) {                                        /* (the rail only opens the page once there is one) */
        draw_text(&c, CX - text_w("NO VISITORS YET", 3) / 2, WC_Y + 300, 3, MSP_DIM, "NO VISITORS YET");
        return;
    }
    const coll_entry_t *e = collection_get(g_coll_sel);
    fish_t f; entry_fish(&f, e);
    src_t band = src_color(INK, 1.0f);
    for (int y = Y; y < Y + 40; y++) span(&c, X, X + W - 1, y, &band, 255);
    draw_text(&c, CX - text_w("POCKET TANK PASSPORT", 3) / 2, Y + 10, 3, GOLD, "POCKET TANK PASSPORT");
    const int PY = Y + 56, PH = 150;
    rect_fill(&c, X, PY, W, PH, 0x0a1824); rect_edge(&c, X, PY, W, PH, MSP_TEAL);
    render_fish_portrait(fb, stride, CX - PAGE_X, PY + PH / 2.0f - PAGE_Y, 2.2f, &f, W - 80, t->clock);
    draw_text(&c, CX - text_w(f.name, 5) / 2, PY + PH + 16, 5, 0xffffff, f.name);
    static const char *const STAGE_W[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
    char home[24], form[32], met[24], here[24], visits[24], fry[24];
    snprintf(home, sizeof home, "%s TANK", e->badge >= 0 ? BADGE_WORDS[e->badge % LINK_BADGES] : "FAR");
    snprintf(form, sizeof form, "%s %s", SHAPE_NAMES[e->shape % SHAPE_N], FINS_NAMES[e->fins % FINS_N]);
    fmt_met(met, sizeof met, e->met_unix); fmt_dur(here, sizeof here, e->here_s);
    snprintf(visits, sizeof visits, "%u VISIT%s", (unsigned)e->visits, e->visits == 1 ? "" : "S");
    snprintf(fry, sizeof fry, "%u FRY HERE", (unsigned)e->fry);
    const int FY = PY + PH + 72, DY = 58, C2 = X + W / 2 + 8;
    draw_text(&c, X, FY, 2, 0x7fa3b5, "HOME");
    if (e->badge >= 0) { link_badge_draw(&c, X + 11, FY + 30, 10, e->badge, 255); draw_text(&c, X + 30, FY + 20, 3, 0xffffff, home); }
    else draw_text(&c, X, FY + 20, 3, 0xffffff, home);
    wv_field(&c, X, FY + DY, "FORM", form);
    wv_field(&c, X, FY + 2 * DY, "STAGE", STAGE_W[e->stage & 3]);
    wv_field(&c, C2, FY + 2 * DY, "VISITS", visits);
    wv_field(&c, X, FY + 3 * DY, "FIRST MET", met);
    wv_field(&c, X, FY + 4 * DY, "IN YOUR TANK", here);
    if (e->fry) wv_field(&c, C2, FY + 4 * DY, "FAMILY", fry);
    const char *st = "COLLECTED";                               /* the stamp, as on its card in the tank */
    int sw = text_w(st, 3) + 20, sx0 = X + W - sw, sy0 = WC_Y + WC_H - 36 - 20 - 36;   /* low on the page, over the code line: clear of every field (TRIANGLE TANK runs wide) */
    rect_edge(&c, sx0, sy0, sw, 36, GOLD); rect_edge(&c, sx0 + 2, sy0 + 2, sw - 4, 32, GOLD);
    draw_text(&c, sx0 + 10, sy0 + 8, 3, GOLD, st);
    char m1[64]; int cols = W / 12; if (cols > 60) cols = 60;
    snprintf(m1, sizeof m1, "P<FISH<<%s<<%08X", f.name, (unsigned)e->uid);
    for (int j = (int)strlen(m1); j < cols; j++) m1[j] = '<';
    m1[cols] = 0;
    draw_text(&c, X, WC_Y + WC_H - 36, 2, 0xc8d6de, m1);
}
int render_collection_tap(const tank_t *t, float x, float y) {
    (void)t;
    int rh = w_rail_hit(x, y);
    if (rh == WR_N) return COLL_TAP_TANK;
    if (rh == WR_KEEPER) return COLL_TAP_CLOSE;                  /* back to the milestones page, as the small page's CLOSE */
    if (rh == WR_UPGRADES) return COLL_TAP_SHOP;
    if (rh == WR_SETTINGS) return COLL_TAP_SETTINGS;
    if (rh == WR_VISITORS) return COLL_TAP_KEPT;
    if (x >= WV_X0 && x < WV_X0 + WV_COLS * WV_CW && y >= WV_Y0 && y < WV_Y0 + (COLL_MAX / WV_COLS) * WV_CH) {
        int i = (int)((y - WV_Y0) / WV_CH) * WV_COLS + (int)((x - WV_X0) / WV_CW);
        if (i < collection_count()) { g_coll_sel = i; return COLL_TAP_KEPT; }
        return COLL_TAP_NONE;
    }
    return x >= WD_X ? COLL_TAP_KEPT : COLL_TAP_NONE;
}
bool render_collection_where(int i, int *x, int *y) {
    if (i < 0 || i >= COLL_MAX) return false;
    if (x) *x = WV_X0 + (i % WV_COLS) * WV_CW + WV_CW / 2;
    if (y) *y = WV_Y0 + (i / WV_COLS) * WV_CH + WV_CH / 2;
    return true;
}
