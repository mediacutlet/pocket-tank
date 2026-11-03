/* setup.c — first-run setup flow (see setup.h). */
#include "collection.h"
#include "setup.h"
#include "tank_events.h"
#include "render.h"
#include "progression.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* the reset prompt's palette: ink panel, teal edge, calm / lit buttons */
#define C_PANEL  0x04141a
#define C_EDGE   0x9fd8e2
#define C_INNER  0x1c2f36
#define C_DIM    0x2a3f45
#define C_KEY    0x0e2229
#define C_TEXT   0xffffff
#define C_CAPT   0x9fd8e2
#define C_GO     0x155e58      /* BEGIN: the teal of a charging battery */
#define C_GO_E   0x38dcc7
#define C_FILL   0x5f8a92      /* the family page's trait bars: a muted teal the parents' ticks stand out on */
#define C_NEW    0xffd166      /* a mutation (0.4): gold - NEW!, the announcement's line and ring */

static bool s_active;
static bool s_birth;           /* the birth flow (three pages about s_fish), not the first run */
static bool s_place;           /* the placement page (one page about s_item) */
static bool s_moved;           /* ... and its piece has been dragged, re-ranked, re-depthed or recolored on it: drawn LIVE until the page
                                  closes (render_tank). Until then it stays in the scene as it was - opening a page, reading it and DONE
                                  cost nothing (0.3.9.4, Strato on the square: a page up dropped the frame rate "to a crawl") */
static bool s_rename;          /* the rename page (one page about s_fish: the letter wheel, CANCEL / DONE) */
static char s_old_name[FISH_NAME_MAX + 1];   /* ... the name it came in with (CANCEL puts it back) */
static int  s_renamed;         /* a rename page just closed: the fish's slot + 1, until the platform takes it */
static int  s_item;            /* placement: the SD item being placed */
static int  s_ref = -1;        /* placement: the piece the ORDER SQUARES show (the sword plant's page, 0.4) - the one it last passed, -1 = the default */
static int  order_ref(const tank_t *t);   /* (below, with the squares) */
static int  s_fish;            /* birth flow: the newborn's slot */
static int  s_birth_offered = -1;   /* setup_poll_birth: the arrival already opened for (-1 = none since boot) */
static int  s_page;
static int  s_slot;            /* name pages: the slot the wheel turns */
/* the finger (setup_touch) */
static bool  s_down;
static float s_px, s_py;       /* press point */
static float s_ly;             /* last y, for the wheel */
static float s_acc;            /* vertical travel toward the next step */
static bool  s_spun;           /* this press has turned the wheel (or moved the column): no tap on release */
static bool  s_sell_armed;     /* the placement page's SELL tapped once (2026-09-24) */
static double s_sell_clock;    /* ... when: a second tap within SETUP_SELL_WAIT_S is the same press doubled, not the yes (0.4, the
                                  11-02 sweep: a double-tap sold the piece - after the pumpkin leaves the shop, for good) */
#define SETUP_SELL_WAIT_S 0.6f   /* as the fish card's SELL (render.c MSP_SELL_WAIT_S) */
static int   s_same_note;      /* the editor (0.4): frames left of "SAME AS ITS BODY" after a tap on the struck-out swatch */
/* the legacy keyboards (setup.h, SETUP_KBD_*) */
static int  s_kbd;             /* the name page's design: the wheel unless the director says otherwise */
static bool s_typed;           /* this name page: the keeper has typed (the default is gone) */
static int  s_kb_page;         /* SETUP_KBD_PAGES: 0 = A-M, 1 = N-Z */

static int page_fish(void) { return s_birth || s_rename ? s_fish : s_place ? 0 : s_page <= SETUP_PG_NAME_A ? 0 : 1; }
static bool page_is_name(void) { return s_page == SETUP_PG_NAME_A || s_page == SETUP_PG_NAME_B || s_page == SETUP_PG_NAME_NEW || s_page == SETUP_PG_RENAME; }
static bool page_is_form(void) { return s_page == SETUP_PG_FORM_A || s_page == SETUP_PG_FORM_B; }
static bool page_is_colors(void) { return s_page == SETUP_PG_COLORS_A || s_page == SETUP_PG_COLORS_B; }
static bool page_is_look(void) { return page_is_form() || page_is_colors(); }   /* the editor's two pages */
static bool page_is_last(void) { return s_page == SETUP_PG_CARE || s_page == SETUP_PG_NATURE; }
static bool page_one_button(void) { return s_page == SETUP_PG_WELCOME || s_page == SETUP_PG_BORN; }   /* a lone NEXT at the foot */
static int  first_page(void) { return s_rename ? SETUP_PG_RENAME : s_place ? SETUP_PG_PLACE : s_birth ? SETUP_PG_BORN : SETUP_PG_WELCOME; }
static bool nav_on_top(void) { return (page_is_name() && s_kbd != SETUP_KBD_GRID) || page_is_look() || s_page == SETUP_PG_BUBBLES || s_page == SETUP_PG_PLACE; }
static int  place_y(void) { return s_item == 3 ? SETUP_PLACE_CORAL_Y : s_item == 4 ? SETUP_PLACE_CLUSTER_Y : SETUP_PLACE_Y; }   /* where the drag zone starts */
/* a fish's parent w (0 = parent_uid[0] / parent_a, 1 = the other), wherever it
   lives (2026-10-06): one of ours by its ID - away or not - else a fish of
   another tank by name (kin: a visitor, a cross-tank fry's other parent),
   else "?". The slot index alone named a parent from next door "?" */
typedef struct { const char *name; uint32_t color; float bold, sociable; bool known; } parent_t;
static parent_t parent_of(const tank_t *t, const fish_t *f, int w) {
    parent_t p = { "?", C_DIM, 0, 0, false };
    uint32_t uid = f->parent_uid[w & 1];
    int slot = w ? f->parent_b : f->parent_a;
    const fish_t *q = slot >= 0 && slot < t->n_fish ? &t->fish[slot] : NULL;
    if (!q && uid) for (int i = 0; i < t->n_fish && !q; i++) if (t->fish[i].uid == uid) q = &t->fish[i];
    if (q) { p.name = q->name; p.color = q->color; p.bold = q->bold; p.sociable = q->sociable; p.known = true; return p; }
    const kin_t *k = kin_find(uid);
    if (k) { p.name = k->name; p.color = k->color; p.bold = k->bold; p.sociable = k->sociable; p.known = true; }
    return p;
}
/* a mutation in words for the announcement (0.4): "SOMETHING NEW: SAIL FINS";
   false = none */
static bool mutation_words(const fish_t *f, char *out, size_t n) {
    if (!f->mutated || f->mutated > GENE_N) return false;
    static const char *const PAT[PATTERN_N] = { "BARS", "A MASK", "AN EYE BAND", "NO MARKINGS" };
    switch (f->mutated - 1) {
    case GENE_SHAPE:   snprintf(out, n, "SOMETHING NEW: A %s BODY", SHAPE_NAMES[f->shape]); break;
    case GENE_FINS:    snprintf(out, n, "SOMETHING NEW: %s FINS", FINS_NAMES[f->fins]); break;
    case GENE_PATTERN: snprintf(out, n, "SOMETHING NEW: %s", PAT[f->pattern < PATTERN_N ? f->pattern : 0]); break;
    case GENE_BODY:    snprintf(out, n, "SOMETHING NEW: ITS BODY COLOR"); break;
    default:           snprintf(out, n, "SOMETHING NEW: ITS MARKINGS"); break;
    }
    return true;
}
static void stage(tank_t *t);
static void tidy_name(tank_t *t, int fish);
/* a name page whose fish is not grown yet shows its GROWN UP form beside it */
static bool name_beside(const tank_t *t) { int f = page_fish(); return f >= 0 && f < t->n_fish && t->fish[f].stage < STAGE_ADULT; }

void setup_begin(tank_t *t) {
    if (t->n_fish < 2) { progression_setup_done(t); s_active = false; return; }   /* nothing to name */
    s_active = true; s_birth = false; s_place = false; s_rename = false; s_fish = -1; s_page = SETUP_PG_WELCOME; s_slot = 0; s_down = false;
    tank_emit(TEV_WELCOME, -1);
}
void setup_begin_birth(tank_t *t, int slot) {
    if (slot < 0 || slot >= t->n_fish) { s_active = false; return; }
    s_active = true; s_birth = true; s_place = false; s_rename = false; s_fish = slot; s_page = SETUP_PG_BORN; s_slot = 0; s_down = false;
}
void setup_begin_rename(tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish || s_active) return;
    s_active = true; s_birth = false; s_place = false; s_rename = true; s_fish = fish; s_page = SETUP_PG_RENAME; s_slot = 0; s_down = false;
    s_typed = false; s_kb_page = 0; s_renamed = 0;
    memcpy(s_old_name, t->fish[fish].name, sizeof s_old_name);
    stage(t);
}
bool setup_is_rename(void) { return s_active && s_rename; }
int  setup_take_renamed(void) { int f = s_renamed - 1; s_renamed = 0; return f; }
void setup_begin_place(tank_t *t, int item) {
    if (!tank_decor_placeable(item)) return;
    s_active = true; s_birth = false; s_place = true; s_rename = false; s_item = item; s_fish = -1; s_page = SETUP_PG_PLACE; s_slot = 0; s_down = false;
    s_sell_armed = false; s_moved = false; s_ref = -1;
    stage(t);
}
int setup_poll_birth(tank_t *t) {
    int nb = progression_newborn();
    if (nb < 0) { s_birth_offered = -1; return -1; }    /* nothing owed: a later arrival in the same slot is new */
    if (s_active || nb == s_birth_offered) return -1;   /* a flow is up, or this one was offered (and dropped) already */
    s_birth_offered = nb;
    if (nb >= t->n_fish) { progression_newborn_done(t); return -1; }   /* a stale debt (a staged tank) */
    setup_begin_birth(t, nb);
    return nb;
}
bool setup_birth_due(void) {                               /* a welcome up, or owed and not yet offered */
    int nb = progression_newborn();
    return setup_is_birth() || (nb >= 0 && nb != s_birth_offered);
}
bool setup_active(void) { return s_active; }
bool setup_is_birth(void) { return s_active && s_birth; }
bool setup_is_place(void) { return s_active && s_place; }
bool setup_place_live(void) { return s_active && s_place && s_moved; }
int  setup_item(void)     { return s_active && s_place ? s_item : -1; }
int  setup_fish(void) { return s_active && (page_is_name() || page_is_look() || s_birth) ? page_fish() : -1; }
/* the rename page closes: DONE keeps the wheel's name (tidied; nothing left =
   the preset's) and saves it, anything else puts the old one back. Either
   way the platform is owed the way back to the fish's card (setup_take_renamed). */
static void rename_end(tank_t *t, bool keep) {
    if (s_fish >= 0 && s_fish < t->n_fish) {
        if (keep) { tidy_name(t, s_fish); progression_save(t); }
        else tank_set_name(t, s_fish, s_old_name);
        s_renamed = s_fish + 1;
    }
    s_active = false; s_rename = false; stage(t);
}
static void stage(tank_t *t) {                          /* who is on stage, and where */
    if (s_active && (page_is_name() || page_is_look())) {
        t->stage_fish = (int8_t)page_fish();
        bool beside = !page_is_name() || name_beside(t);    /* NOW at the left, GROWN UP at the right */
        t->stage_x = beside ? SETUP_STAGE_EDIT_X : SETUP_STAGE_X;
        t->stage_y = !page_is_name() ? SETUP_STAGE_EDIT_Y : beside ? PAGE_Y + SETUP_NAME_PREVIEW_Y : SETUP_STAGE_NAME_Y;
    } else t->stage_fish = -1;
}
void setup_cancel(tank_t *t) {
    if (s_active && s_rename) { rename_end(t, false); s_renamed = 0; return; }   /* the old name back, no card to return to */
    if (s_active && s_place) progression_save(t);       /* the piece stays where it was dragged */
    s_active = false; stage(t);
}
/* a fish left and the slots shifted (progression: a sale, a gift): the page about a fish follows it; a page about the one
   that left closes - its rename's CANCEL never names another fish (the 11-02 sweep) */
void setup_fish_removed(tank_t *t, int idx) {
    if (!s_active || !(s_birth || s_rename) || s_fish < 0) return;
    if (s_fish == idx) { s_active = false; s_birth = false; s_rename = false; s_renamed = 0; s_fish = -1; stage(t); return; }
    if (s_fish > idx) { s_fish--; stage(t); }
}
int  setup_page(void)   { return s_page; }
int  setup_slot(void)   { return s_slot; }
void setup_set_keyboard(int mode) { s_kbd = mode == SETUP_KBD_GRID || mode == SETUP_KBD_PAGES ? mode : SETUP_KBD_WHEEL; s_typed = false; s_kb_page = 0; }
int  setup_keyboard(void) { return s_kbd; }

/* ---- the legacy keyboards: geometry and hit test as they were on 2026-09-13 ----
 * GRID: A-Z in 7 columns at a 50 x 38 px pitch, the last row V..Z + a
 * double-width DEL. PAGES: 5 x 3 cells at 72 x 80, the page's 13 letters,
 * then DEL, then the flip in the corner. Both: a tap anywhere over the block
 * picks the NEAREST key. */
#define KG_COLS 7
#define KG_X  (SETUP_X + 17)
#define KG_Y  (SETUP_Y + 116)
#define KG_PX 50
#define KG_PY 38
#define KG_W  46
#define KG_H  34
#define KP_COLS 5
#define KP_ROWS 3
#define KP_PX 72
#define KP_PY 80
#define KP_W  66
#define KP_H  74
#define KP_X  (SETUP_X + (SETUP_W - (KP_COLS - 1) * KP_PX - KP_W) / 2)
#define KP_Y  (SETUP_Y + 76)
#define KP_HALF 13
static int kg_row(int k) { return k == SETUP_KEY_DEL ? 3 : k / KG_COLS; }
static int kg_col(int k) { return k == SETUP_KEY_DEL ? 5 : k % KG_COLS; }
static int kg_w(int k)   { return k == SETUP_KEY_DEL ? KG_W + KG_PX : KG_W; }
static void kg_center(int k, float *x, float *y) {
    *x = KG_X + kg_col(k) * KG_PX + kg_w(k) * 0.5f;
    *y = KG_Y + kg_row(k) * KG_PY + KG_H * 0.5f;
}
static int kp_key(int cell) { return cell < KP_HALF ? cell + s_kb_page * KP_HALF : cell == KP_HALF ? SETUP_KEY_DEL : SETUP_KEY_PAGE; }
static int kbd_hit(float x, float y) {
    int best = -1; float bd = 1e9f;
    if (s_kbd == SETUP_KBD_GRID) {
        int kw = KG_COLS * KG_PX - (KG_PX - KG_W), kh = 4 * KG_PY - (KG_PY - KG_H);
        if (x < KG_X - 6 || x >= KG_X + kw + 6 || y < KG_Y - 6 || y >= KG_Y + kh + 6) return 0;
        for (int k = 0; k <= SETUP_KEY_DEL; k++) {
            float kx, ky; kg_center(k, &kx, &ky);
            float dx = x - kx, dy = (y - ky) * 1.3f;    /* rows are tighter than columns */
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = k; }
        }
    } else {
        int kw = (KP_COLS - 1) * KP_PX + KP_W, kh = (KP_ROWS - 1) * KP_PY + KP_H;
        if (x < KP_X - 16 || x >= KP_X + kw + 16 || y < KP_Y - 16 || y >= KP_Y + kh + 16) return 0;
        for (int cell = 0; cell < KP_COLS * KP_ROWS; cell++) {
            float dx = x - (KP_X + (cell % KP_COLS) * KP_PX + KP_W * 0.5f), dy = y - (KP_Y + (cell / KP_COLS) * KP_PY + KP_H * 0.5f);
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = kp_key(cell); }
        }
    }
    return best < 0 ? 0 : SETUP_HIT_KEY0 + best;
}
static void kbd_key(fish_t *f, int k) {
    int n = (int)strlen(f->name);
    if (k == SETUP_KEY_PAGE) { s_kb_page ^= 1; return; }
    if (k == SETUP_KEY_DEL) {
        if (!s_typed) { f->name[0] = 0; s_typed = true; }              /* the default goes in one stroke */
        else if (n) f->name[n - 1] = 0;
        return;
    }
    if (!s_typed) { f->name[0] = 0; n = 0; s_typed = true; }          /* first letter replaces the default */
    if (n < FISH_NAME_MAX) { f->name[n] = (char)('a' + k); f->name[n + 1] = 0; }   /* lowercase inside; the font draws upper */
}

const char *setup_hit_name(int id) {
    static char buf[12];
    if (id == SETUP_HIT_NEXT) return "NEXT";
    if (id == SETUP_HIT_BACK) return "BACK";
    if (id == SETUP_HIT_SELL) return "SELL";
    if (id == SETUP_HIT_ROLL) return "ROLL";
    if (id == SETUP_HIT_SWAP) return "SWAP";
    if (id >= SETUP_HIT_ACC0 && id < SETUP_HIT_ACC0 + LOOK_N) { snprintf(buf, sizeof buf, "markings %d", id - SETUP_HIT_ACC0); return buf; }
    if (id >= SETUP_HIT_SHAPE0 && id < SETUP_HIT_SHAPE0 + SHAPE_N) return SHAPE_NAMES[id - SETUP_HIT_SHAPE0];
    if (id >= SETUP_HIT_FINS0 && id < SETUP_HIT_FINS0 + FINS_N) return FINS_NAMES[id - SETUP_HIT_FINS0];
    if (id >= SETUP_HIT_PAT0 && id < SETUP_HIT_PAT0 + PATTERN_N) return PATTERN_NAMES[id - SETUP_HIT_PAT0];
    if (id == SETUP_HIT_SHELVE) return "SHELVE";
    if (id == SETUP_HIT_LOCK) return "LOCK";
    if (id == SETUP_HIT_BCK) return "SEND BACK";
    if (id == SETUP_HIT_FWD) return "BRING FORWARD";
    if (id == SETUP_HIT_ORD0) return "BEHIND (the order squares)";
    if (id == SETUP_HIT_ORD0 + 1) return "IN FRONT (the order squares)";
    if (id == SETUP_HIT_UP)   return "up";
    if (id == SETUP_HIT_DOWN) return "down";
    if (id >= SETUP_HIT_SLOT0 && id < SETUP_HIT_SLOT0 + FISH_NAME_MAX) { snprintf(buf, sizeof buf, "slot %d", id - SETUP_HIT_SLOT0); return buf; }
    if (id == SETUP_HIT_KEY0 + SETUP_KEY_DEL) return "DEL";
    if (id == SETUP_HIT_KEY0 + SETUP_KEY_PAGE) return "page flip";
    if (id >= SETUP_HIT_KEY0 && id < SETUP_HIT_KEY0 + 26) { snprintf(buf, sizeof buf, "key %c", 'A' + id - SETUP_HIT_KEY0); return buf; }
    if (id >= SETUP_HIT_BODY0 && id < SETUP_HIT_BODY0 + LOOK_N) { snprintf(buf, sizeof buf, "body %d", id - SETUP_HIT_BODY0); return buf; }
    if (id >= SETUP_HIT_Z0 && id < SETUP_HIT_Z0 + DECOR_Z_N) return id == SETUP_HIT_Z0 ? "BEHIND" : id == SETUP_HIT_Z0 + 1 ? "AMONG" : "IN FRONT";
    return "nothing";
}

/* ---- the letter wheel over a fish's name ----
 * A slot holds one of 27 values: blank, A..Z (stored lowercase). The name string carries the
 * slots up to the last letter (blanks inside it are spaces); NEXT / BACK
 * tidy it - trailing blanks dropped, inner ones closed up, nothing left =
 * the preset's name again. */
static int slot_val(const fish_t *f, int i) {           /* 0 = blank, 1..26 = A..Z */
    if (i >= (int)strlen(f->name)) return 0;
    char ch = f->name[i];
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 1 : 0;
}
static void slot_set(fish_t *f, int i, int v) {
    int n = (int)strlen(f->name);
    if (v == 0) {
        if (i >= n) return;
        f->name[i] = ' ';
        while (n > 0 && f->name[n - 1] == ' ') f->name[--n] = 0;   /* a blank at the end shortens */
        return;
    }
    while (n < i) f->name[n++] = ' ';                               /* (unreachable: slots snap to <= len) */
    f->name[i] = (char)('a' + v - 1);               /* names are lowercase inside (tank_set_name) */
    if (i >= n) f->name[i + 1] = 0;
}
static int name_len(const fish_t *f) { return (int)strlen(f->name); }
static void pick_slot(const fish_t *f, int i) {         /* the first blank is the last pickable slot */
    int n = name_len(f);
    if (i > n) i = n;
    if (i > FISH_NAME_MAX - 1) i = FISH_NAME_MAX - 1;
    if (i < 0) i = 0;
    s_slot = i;
}
static void spin(fish_t *f, int dir) {
    tank_emit(TEV_WHEEL_TICK, -1);
    if (name_len(f) < s_slot) pick_slot(f, s_slot);
    slot_set(f, s_slot, (slot_val(f, s_slot) + 27 + dir) % 27);
}
static void tidy_name(tank_t *t, int fish) {
    fish_t *f = &t->fish[fish];
    char out[FISH_NAME_MAX + 1]; int n = 0;
    for (const char *p = f->name; *p; p++) if (*p != ' ' && n < FISH_NAME_MAX) out[n++] = *p;
    out[n] = 0;
    tank_set_name(t, fish, out);                        /* empty = the preset's */
}

/* a swatch that cannot be picked: a bold ink X corner to corner (the editor's
   markings the body's own color) - a shape, so it reads without color */
static void struck_out(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t under) {
    const int n = w > h ? w : h;
    uint32_t r = under >> 16 & 255, g = under >> 8 & 255, b = under & 255;
    uint32_t ink = r * 299 + g * 587 + b * 114 < 100 * 1000 ? C_CAPT : C_PANEL;   /* light on a dark swatch (charcoal), ink on the rest */
    for (int i = 2; i <= n - 3; i++) {
        int px = x + 3 + (w - 6) * i / n, py = y + 3 + (h - 6) * i / n, qy = y + h - 3 - (h - 6) * i / n;
        render_rect(fb, stride, px - 2, py - 2, 4, 4, ink);
        render_rect(fb, stride, px - 2, qy - 2, 4, 4, ink);
    }
}
/* SWAP (COLORS): a tall key between the palettes, a double-headed arrow -
   up at the body row, down at the markings row: they trade places */
static void swap_button(uint16_t *fb, int stride) {
    const int x = SETUP_SWAP_X, y = SETUP_SWAP_Y, w = SETUP_SWAP_W, h = SETUP_SWAP_H, cx = x + w / 2;
    render_rect(fb, stride, x, y, w, h, C_KEY);
    render_rect_edge(fb, stride, x, y, w, h, C_EDGE);
    const int head = 9;                                  /* each arrowhead: rows 2..18 px wide */
    for (int i = 0; i < head; i++) {
        int half = 1 + i;
        render_rect(fb, stride, cx - half, y + 6 + i, 2 * half, 1, C_TEXT);          /* up, its tip at the top */
        render_rect(fb, stride, cx - half, y + h - 7 - i, 2 * half, 1, C_TEXT);      /* down, its tip at the foot */
    }
    render_rect(fb, stride, cx - 2, y + 6 + head, 4, h - 12 - 2 * head, C_TEXT);    /* the shaft between them */
}
/* the editor's rows (setup.h, THE EDITOR): where each sits and what a pick is */
typedef struct { int y, h, x, px, w, n, hit0, gene; const char *label; } edit_row_t;
static const edit_row_t FORM_ROWS[] = {
    { SETUP_FORM_SHAPE_Y, SETUP_FORM_TILE_H, SETUP_TILE_X, SETUP_TILE_PX, SETUP_TILE_W, SHAPE_N, SETUP_HIT_SHAPE0, GENE_SHAPE, "SHAPE" },
    { SETUP_FORM_FINS_Y,  SETUP_FORM_TILE_H, SETUP_TILE_X, SETUP_TILE_PX, SETUP_TILE_W, FINS_N,  SETUP_HIT_FINS0,  GENE_FINS,  "FINS" },
};
static const edit_row_t COLOR_ROWS[] = {
    { SETUP_COLOR_BODY_Y, SETUP_COLOR_SW_H,  SETUP_SW_X,   SETUP_SW_PX,   SETUP_SW_W,   LOOK_N,    SETUP_HIT_BODY0, GENE_BODY,    "BODY" },
    { SETUP_COLOR_PAT_Y,  SETUP_COLOR_PAT_H, SETUP_PAT_TILE_X, SETUP_PAT_TILE_PX, SETUP_PAT_TILE_W, PATTERN_TILES, SETUP_HIT_PAT0, GENE_PATTERN, "PATTERN" },
    { SETUP_COLOR_ACC_Y,  SETUP_COLOR_SW_H,  SETUP_SW_X,   SETUP_SW_PX,   SETUP_SW_W,   LOOK_N,    SETUP_HIT_ACC0,  GENE_ACCENT,  "MARKINGS" },
};
static int edit_rows(const edit_row_t **r) {
    if (page_is_form()) { *r = FORM_ROWS; return (int)(sizeof FORM_ROWS / sizeof *FORM_ROWS); }
    *r = COLOR_ROWS; return (int)(sizeof COLOR_ROWS / sizeof *COLOR_ROWS);
}
/* the page's genes at random (ROLL): never markings that match the body */
static void edit_roll(tank_t *t, int fish) {
    const edit_row_t *r; int n = edit_rows(&r);
    for (int k = 0; k < n; k++) {
        int nv = r[k].gene == GENE_PATTERN ? PATTERN_N : r[k].n;   /* ROLL can land on SOLID too */
        int v = (int)tank_randf(t, 0, nv - 0.001f);
        if (r[k].gene == GENE_ACCENT)
            for (int guard = 0; guard < 16 && LOOK_ACCENT[v] == t->fish[fish].color; guard++) v = (int)tank_randf(t, 0, r[k].n - 0.001f);
        tank_set_gene(t, fish, r[k].gene, v);
    }
}

static bool in_box(float x, float y, int bx, int by, int bw, int bh, int m) {
    return x >= bx - m && x < bx + bw + m && y >= by - m && y < by + bh + m;
}
static int nearest_slot(float x) {
    int i = (int)((x - SETUP_SLOT_X + (SETUP_SLOT_PX - SETUP_SLOT_W) * 0.5f) / SETUP_SLOT_PX);
    return i < 0 ? 0 : i >= FISH_NAME_MAX ? FISH_NAME_MAX - 1 : i;
}
int setup_hit(float x, float y) {
    if (!s_active) return 0;
    x -= PAGE_X; y -= PAGE_Y;                           /* the page's own coordinates (render.h) */
    const int m = 10;                                   /* a fingertip's slop, as on the reset prompt */
    if (page_one_button())
        return in_box(x, y, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m) ? SETUP_HIT_NEXT : 0;
    if (nav_on_top()) {
        if (s_place) {                                  /* the four-button row (0.3.9): SELL | SHELVE | LOCK | DONE, a narrower width */
            if (in_box(x, y, SETUP_TOP_DONE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
            if (in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_SELL;
            if (in_box(x, y, SETUP_TOP_SHELVE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_SHELVE;
            if (in_box(x, y, SETUP_TOP_LOCK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_LOCK;
        }
        else if (in_box(x, y, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (s_place && in_box(x, y, SETUP_ORD_X0, SETUP_ORD_Y, SETUP_ORD_W, SETUP_BTN_H, 6)) return SETUP_HIT_BCK;   /* (every piece, the sword plant too) */
        if (s_place && tank_decor_z_count(s_item) < 2 && in_box(x, y, SETUP_ORDSQ_X, SETUP_DEPTH_Y, 2 * SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, 6))
            return SETUP_HIT_ORD0 + (x >= SETUP_ORDSQ_X + SETUP_DEPTH_SEG_W);   /* the plant's ORDER SQUARES (no piece beside it: setup_activate does nothing) */
        if (s_place && in_box(x, y, SETUP_ORD_X1, SETUP_ORD_Y, SETUP_ORD_W, SETUP_BTN_H, 6)) return SETUP_HIT_FWD;
        if (!s_place && in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
        if (page_is_look() && in_box(x, y, SETUP_ROLL_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_ROLL;
    } else {
        if (in_box(x, y, SETUP_NEXT_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (in_box(x, y, SETUP_BACK_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
    }
    if (page_is_name() && s_kbd) return kbd_hit(x, y);
    if (page_is_name()) {
        int rw = (FISH_NAME_MAX - 1) * SETUP_SLOT_PX + SETUP_SLOT_W;
        if (x < SETUP_SLOT_X - 24 || x >= SETUP_SLOT_X + rw + 24) return 0;
        /* the row band picks a slot; the bands above and below are the
           chevrons - over the active slot and its neighbours, a hand wide */
        if (y >= SETUP_SLOT_Y - 16 && y < SETUP_SLOT_Y + SETUP_SLOT_H + 20) return SETUP_HIT_SLOT0 + nearest_slot(x);
        float cx = SETUP_SLOT_X + s_slot * SETUP_SLOT_PX + SETUP_SLOT_W * 0.5f;
        if (x < cx - 66 || x > cx + 66) return 0;
        if (y >= SETUP_SLOT_Y - 16 - 70 && y < SETUP_SLOT_Y - 16) return SETUP_HIT_UP;
        if (y >= SETUP_SLOT_Y + SETUP_SLOT_H + 20 && y < SETUP_SLOT_Y + SETUP_SLOT_H + 20 + 70) return SETUP_HIT_DOWN;
        return 0;
    }
    if (s_page == SETUP_PG_PLACE) {
        if (s_item == 4) {                              /* the cluster's LOOK row: the nearest tile along x */
            int rw = (SETUP_LOOK_N - 1) * SETUP_LOOK_PX + SETUP_LOOK_W;
            if (in_box(x, y, SETUP_LOOK_X, SETUP_LOOK_Y, rw, SETUP_LOOK_H, 12)) {
                int i = (int)((x - SETUP_LOOK_X + (SETUP_LOOK_PX - SETUP_LOOK_W) * 0.5f) / SETUP_LOOK_PX);
                if (i < 0) i = 0;
                if (i >= SETUP_LOOK_N) i = SETUP_LOOK_N - 1;
                return SETUP_HIT_COLOR0 + i;
            }
        }
        if (s_item == 3) {                              /* the coral's COLOR row: the nearest swatch along x, a band 12 px around */
            int rw = (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W;
            if (in_box(x, y, SETUP_COL_X, SETUP_COL_Y, rw, SETUP_COL_H, 12)) {
                int i = (int)((x - SETUP_COL_X + (SETUP_COL_PX - SETUP_COL_W) * 0.5f) / SETUP_COL_PX);
                if (i < 0) i = 0;
                if (i >= SETUP_COL_N) i = SETUP_COL_N - 1;
                return SETUP_HIT_COLOR0 + i;
            }
        }
        /* the DEPTH bar: a band 8 px around it, the segment under the finger
           (the item's own depths: the plant's three, the castle's two) */
        int n = tank_decor_z_count(s_item), bw = n * SETUP_DEPTH_SEG_W, bx = (PAGE_W - bw) / 2;
        if (n < 2 || !in_box(x, y, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, 8)) return 0;   /* (the plant: one depth, no bar - 0.4) */
        int i = (int)((x - bx) / SETUP_DEPTH_SEG_W);
        if (i < 0) i = 0;
        if (i >= n) i = n - 1;
        return SETUP_HIT_Z0 + tank_decor_z_at(s_item, i);
    }
    if (page_is_colors() && in_box(x, y, SETUP_SWAP_X, SETUP_SWAP_Y, SETUP_SWAP_W, SETUP_SWAP_H, 6)) return SETUP_HIT_SWAP;   /* before the rows' bands */
    if (page_is_look()) {
        /* the rows: a band from a row's label to halfway into the gap under
           it (the last one 24 px past its foot - fingers land low), the
           nearest choice along x */
        const edit_row_t *r; int n = edit_rows(&r);
        for (int k = 0; k < n; k++) {
            int top = r[k].y - SETUP_LABEL_DY - 4;
            int bot = k + 1 < n ? (r[k].y + r[k].h + r[k + 1].y - SETUP_LABEL_DY - 4) / 2 : r[k].y + r[k].h + 24;
            if (k + 1 < n && bot < r[k].y + r[k].h) bot = r[k].y + r[k].h;
            int rw = (r[k].n - 1) * r[k].px + r[k].w;
            if (y < top || y >= bot || x < r[k].x - 12 || x >= r[k].x + rw + 12) continue;
            int i = (int)((x - r[k].x + (r[k].px - r[k].w) * 0.5f) / r[k].px);
            if (i < 0) i = 0;
            if (i >= r[k].n) i = r[k].n - 1;
            return r[k].hit0 + i;
        }
    }
    return 0;
}

void setup_activate(tank_t *t, int id) {
    if (!s_active || !id) return;
    fish_t *f = &t->fish[page_fish()];
    if (s_place) {                                      /* DONE saves the spot; a layer button sets the depth; SELL twice sells */
        if (id == SETUP_HIT_SELL) {
            if (!s_sell_armed) { s_sell_armed = true; s_sell_clock = t->clock; return; }
            if (t->clock - s_sell_clock < SETUP_SELL_WAIT_S) { s_sell_clock = t->clock; return; }   /* a doubled tap (or a mash): still armed, the wait again from it */
            s_sell_armed = false;
            if (progression_sell(t, s_item)) { s_active = false; tank_emit(TEV_CONFIRM, -1); }
            return;
        }
        if (id == SETUP_HIT_SHELVE) {                       /* the shelf (0.3.9): one tap, nothing to arm - it is undone from the shop's row */
            s_sell_armed = false;
            if (progression_shelve(t, s_item)) { s_active = false; tank_emit(TEV_CONFIRM, -1); }
            return;
        }
        if (id == SETUP_HIT_LOCK) {                         /* the LOCK (0.3.9): a toggle; the page stays up, the button reads LOCKED.
                                                               Once DONE closes it a held finger passes the piece by - the page
                                                               opens from the shop's row only (MOVE), and this button undoes it */
            s_sell_armed = false;
            if (progression_lock(t, s_item, !tank_decor_locked(t, s_item))) tank_emit(TEV_CONFIRM, -1);
            return;
        }
        s_sell_armed = false;                           /* any other tap stands SELL down */
        if (id == SETUP_HIT_NEXT) { s_active = false; tank_emit(TEV_CONFIRM, -1); progression_save(t); }
        else if (id == SETUP_HIT_BCK || id == SETUP_HIT_FWD) {                       /* the stack: a step; DONE saves it */
            const int dir = id == SETUP_HIT_FWD ? 1 : -1, past = tank_decor_neighbor(t, s_item, dir);
            if (tank_decor_shift(t, s_item, dir)) { s_moved = true; s_ref = past; }      /* (the squares follow the piece just passed) */
        }
        else if (id == SETUP_HIT_ORD0 || id == SETUP_HIT_ORD0 + 1) {               /* the ORDER SQUARES: behind / in front of their piece - the arrow's step */
            const int ref = order_ref(t); const bool front = id == SETUP_HIT_ORD0 + 1;
            for (int k = 0; ref >= 0 && k < DECOR_SLOTS && tank_decor_above(t, s_item, ref) != front; k++)
                if (tank_decor_shift(t, s_item, front ? 1 : -1)) s_moved = true; else break;
            if (ref >= 0) s_ref = ref;
        }
        else if (id >= SETUP_HIT_Z0 && id < SETUP_HIT_Z0 + DECOR_Z_N) {
            if (tank_decor_z(t, s_item) != id - SETUP_HIT_Z0) s_moved = true;
            tank_decor_set(t, s_item, tank_decor_x(t, s_item), id - SETUP_HIT_Z0);
        } else if (s_item == 3 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CORAL_N) {
            if (tank_coral_rgb(t) != CORAL_PAL[id - SETUP_HIT_COLOR0]) s_moved = true;
            tank_coral_set_rgb(t, CORAL_PAL[id - SETUP_HIT_COLOR0]);   /* the coral wears it at once; DONE saves it */
        } else if (s_item == 4 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CLUSTER_SCHEME_N) {
            if (tank_cluster_scheme(t) != id - SETUP_HIT_COLOR0) s_moved = true;
            tank_cluster_set_scheme(t, id - SETUP_HIT_COLOR0);
        }
        return;
    }
    if (s_rename) {                                     /* one page: DONE keeps the name, CANCEL the old one; the wheel below */
        if (id == SETUP_HIT_NEXT) { rename_end(t, true); tank_emit(TEV_CONFIRM, -1); return; }
        if (id == SETUP_HIT_BACK) { rename_end(t, false); return; }
    }
    if (id == SETUP_HIT_NEXT) {
        if (page_is_name()) tidy_name(t, page_fish());
        if (page_is_last()) {                           /* BEGIN / DONE: the debt is paid, the names saved */
            s_active = false; stage(t);
            tank_emit(TEV_CONFIRM, -1);
            if (s_birth) progression_newborn_done(t); else progression_setup_done(t);
            return;
        }
        s_page++; s_slot = 0; s_typed = false; s_kb_page = 0; s_same_note = 0; stage(t);
        return;
    }
    if (id == SETUP_HIT_BACK) {
        if (page_is_name()) tidy_name(t, page_fish());
        if (s_page > first_page()) s_page--;
        s_slot = 0; s_typed = false; s_kb_page = 0; s_same_note = 0; stage(t);
        return;
    }
    if (page_is_name() && s_kbd) {
        if (id >= SETUP_HIT_KEY0 && id < SETUP_HIT_KEY0 + SETUP_KEY_N) kbd_key(f, id - SETUP_HIT_KEY0);
        return;
    }
    if (page_is_name()) {
        if (id >= SETUP_HIT_SLOT0 && id < SETUP_HIT_SLOT0 + FISH_NAME_MAX) pick_slot(f, id - SETUP_HIT_SLOT0);
        else if (id == SETUP_HIT_UP)   spin(f, +1);
        else if (id == SETUP_HIT_DOWN) spin(f, -1);
        return;
    }
    if (page_is_look()) {                               /* the fish wears a pick at once: the fry on stage, the grown portrait */
        if (id == SETUP_HIT_ROLL) { edit_roll(t, page_fish()); tank_emit(TEV_WHEEL_TICK, -1); return; }
        if (id == SETUP_HIT_SWAP && page_is_colors()) {       /* body <-> markings: one palette, so exact */
            int b = tank_gene(f, GENE_BODY), a = tank_gene(f, GENE_ACCENT);
            tank_set_look(t, page_fish(), LOOK_COLORS[a], LOOK_COLORS[b]);
            tank_emit(TEV_WHEEL_TICK, -1); return;
        }
        const edit_row_t *r; int n = edit_rows(&r);
        for (int k = 0; k < n; k++)
            if (id >= r[k].hit0 && id < r[k].hit0 + r[k].n) {
                int v = id - r[k].hit0;
                if (r[k].gene == GENE_ACCENT && LOOK_ACCENT[v] == f->color) { s_same_note = 75; return; }   /* struck out: say why (~3 s) */
                if (r[k].gene == GENE_PATTERN && v == f->pattern) v = PATTERN_SOLID;   /* the picked tile again: deselected - solid */
                tank_set_gene(t, page_fish(), r[k].gene, v);
                return;
            }
    }
}

void setup_touch(tank_t *t, float x, float y, bool down) {
    stage(t);                                           /* every frame: the tank keeps the fish on stage */
    if (!s_active) { s_down = down; return; }
    if (down && !s_down) {                              /* press */
        s_px = x; s_py = y; s_ly = y; s_acc = 0; s_spun = false;
        int h = setup_hit(x, y);
        if (page_is_name() && h >= SETUP_HIT_SLOT0 && h < SETUP_HIT_SLOT0 + FISH_NAME_MAX)
            pick_slot(&t->fish[page_fish()], h - SETUP_HIT_SLOT0);   /* picked on touch: the drag turns it */
        if (s_page == SETUP_PG_BUBBLES && !h && y - PAGE_Y > SETUP_TOP_BTN_Y + SETUP_BTN_H + 8) {
            tank_set_bubble_x(t, x); s_spun = true;         /* the column comes to the finger */
        }
        if (s_page == SETUP_PG_PLACE && !h && y - PAGE_Y > place_y()) {
            tank_decor_set(t, s_item, x, tank_decor_z(t, s_item)); s_spun = true; s_moved = true;   /* the piece comes to the finger */
        }
    } else if (down && s_page == SETUP_PG_BUBBLES && s_spun) {
        tank_set_bubble_x(t, x);                            /* ... and follows it */
    } else if (down && s_page == SETUP_PG_PLACE && s_spun) {
        tank_decor_set(t, s_item, x, tank_decor_z(t, s_item)); s_moved = true;
    } else if (down && page_is_name()) {                /* the wheel: vertical travel spins the letter */
        int h0 = setup_hit(s_px, s_py);
        if (h0 >= SETUP_HIT_SLOT0 && h0 < SETUP_HIT_SLOT0 + FISH_NAME_MAX) {
            s_acc += y - s_ly;
            while (s_acc <= -SETUP_SPIN_PX) { spin(&t->fish[page_fish()], +1); s_acc += SETUP_SPIN_PX; s_spun = true; }   /* up = next letter */
            while (s_acc >=  SETUP_SPIN_PX) { spin(&t->fish[page_fish()], -1); s_acc -= SETUP_SPIN_PX; s_spun = true; }
        }
        s_ly = y;
    } else if (!down && s_down) {                       /* release: a tap, unless the wheel turned */
        if (!s_spun) {
            int h = setup_hit(s_px, s_py);
            if (h && h == setup_hit(x, y)) setup_activate(t, h);
        }
    }
    s_down = down;
}

/* ---- drawing ---- */
static void text_c(uint16_t *fb, int stride, int cx, int y, int scale, uint32_t rgb, const char *s) {
    render_text(fb, stride, cx - render_text_w(s, scale) / 2, y, scale, rgb, s);
}
static void lines_c(uint16_t *fb, int stride, int y, int pitch, uint32_t rgb, const char *const *ls, int n) {
    for (int i = 0; i < n; i++) text_c(fb, stride, SETUP_X + SETUP_W / 2, y + i * pitch, 2, rgb, ls[i]);
}
static void nav(uint16_t *fb, int stride, bool top, const char *next_label, bool go) {
    int y = top ? SETUP_TOP_BTN_Y : SETUP_BTN_Y, w = top ? SETUP_TOP_BTN_W : SETUP_BTN_W;
    render_button(fb, stride, top ? SETUP_TOP_BACK_X : SETUP_BACK_X, y, w, SETUP_BTN_H, C_KEY, C_DIM, s_rename ? "CANCEL" : "BACK", 2);
    render_button(fb, stride, top ? SETUP_TOP_NEXT_X : SETUP_NEXT_X, y, w, SETUP_BTN_H,
                  go ? C_GO : C_INNER, go ? C_GO_E : C_EDGE, next_label, 2);
}
/* page dots along the foot: where the keeper is in the flow */
static void dots(uint16_t *fb, int stride, int y) {
    if (s_place || s_rename) return;                    /* one page: nothing to count */
    int n = s_birth ? SETUP_BIRTH_PAGES : SETUP_PG_N, cur = s_page - first_page();
    int w = n * 10 - 4, x0 = (PAGE_W - w) / 2;
    for (int i = 0; i < n; i++) render_rect(fb, stride, x0 + i * 10, y, 6, 3, i == cur ? C_EDGE : C_DIM);
}
/* a ring round a fish where it swims: the fish is in the TANK, the drawing on
   the page - on a glass the page does not start at (the bowl, the watch) the
   ring sat PAGE_X / PAGE_Y off its fish until 2026-10-01 */
static void fish_ring(uint16_t *fb, int stride, const fish_t *f, float r, uint32_t rgb) {
    render_ring(fb, stride, f->x - PAGE_X, f->y - PAGE_Y, r, rgb);
}
/* a caption with a name in it, the name in its own colour: `pre` NAME `post` */
static void text_named(uint16_t *fb, int stride, int x, int y, const char *pre, const char *name, uint32_t name_rgb, const char *post) {
    render_text(fb, stride, x, y, 2, C_CAPT, pre); x += render_text_w(pre, 2);
    render_text(fb, stride, x, y, 2, name_rgb, name); x += render_text_w(name, 2);
    if (post) render_text(fb, stride, x, y, 2, C_CAPT, post);
}
static uint32_t dim(uint32_t c, int pct) {              /* c toward the ink, pct% of it left */
    return ((c >> 16 & 255) * pct / 100) << 16 | ((c >> 8 & 255) * pct / 100) << 8 | (c & 255) * pct / 100;
}
/* a fish's own colour as the colour of its name on the dark panel: a dark fish (the ink accent comes through
 * as a body) has its name in light gray instead - black on the panel could not be read (0.3.9.3, Strato) */
static uint32_t name_rgb(uint32_t c) {
    int luma = ((c >> 16 & 255) * 299 + (c >> 8 & 255) * 587 + (c & 255) * 114) / 1000;
    return luma < 90 ? 0xd0d8db : c;
}
/* a chevron of 4x4 blocks, `up` pointing up, apex at (cx, y) */
static void chevron(uint16_t *fb, int stride, int cx, int y, bool up, uint32_t rgb) {
    for (int i = 0; i < 5; i++) {
        int yy = up ? y + i * 4 : y - i * 4;
        render_rect(fb, stride, cx - 4 - i * 4, yy, 4, 4, rgb);
        render_rect(fb, stride, cx + i * 4, yy, 4, 4, rgb);
    }
}
/* a small sword leaf for the depth tiles: `h` px tall on the spine x, the
 * plant's own lanceolate taper (widest at 40% up) in its yellow-green */
static void leaf_glyph(uint16_t *fb, int stride, int cx, int y_bot, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) {
        float u = i / (float)(h - 1);
        float half = 0.6f + 4.4f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f);
        int w = (int)(half * 2 + 0.5f); if (w < 1) w = 1;
        render_rect(fb, stride, cx - w / 2, y_bot - i, w, 1, rgb);
    }
}
/* a thin seagrass frond for the sword plant's depth tiles (0.3.9.4): 2 px wide, a gentle wave, the seagrass's green */
static void frond_glyph(uint16_t *fb, int stride, int cx, int y_bot, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) render_rect(fb, stride, cx + (int)lroundf(1.6f * sinf(i * 0.28f + cx * 0.7f)), y_bot - i, 2, 1, rgb);
}
/* a little castle for the castle's depth tiles: two towers, a wall with
 * merlons, the arch - the stone tones of the real one */
/* a little coral for its depth tiles: a trunk and four branches, rounded
 * with 2 px blocks, in the keeper's colour with a lit tip */
static void coral_glyph(uint16_t *fb, int stride, int cx, int y_bot, uint32_t rgb) {
    uint32_t lit = ((rgb >> 16 & 255) * 3 / 4 + 64) << 16 | ((rgb >> 8 & 255) * 3 / 4 + 64) << 8 | ((rgb & 255) * 3 / 4 + 40);
    render_rect(fb, stride, cx - 2, y_bot - 26, 4, 27, rgb);                       /* the trunk */
    render_rect(fb, stride, cx - 2, y_bot - 28, 4, 2, lit);
    static const int8_t br[4][3] = { { -1, 8, 7 }, { 1, 13, 6 }, { -1, 17, 5 }, { 1, 21, 4 } };   /* side, height, length */
    for (int k = 0; k < 4; k++) {
        int s = br[k][0], y = y_bot - br[k][1], n = br[k][2];
        for (int i = 0; i < n; i++) render_rect(fb, stride, cx + (s > 0 ? 2 + i * 2 : -4 - i * 2), y - i, 2, 3, i == n - 1 ? lit : rgb);
    }
}
/* a little reef cluster for its depth tiles: stones, a tube, a coral sprig,
 * a brain lump, in the look's colours */
static void cluster_glyph(uint16_t *fb, int stride, int cx, int y_bot, const cluster_scheme_t *sc) {
    render_rect(fb, stride, cx - 20, y_bot - 5, 40, 6, 0x7c756a);
    render_rect(fb, stride, cx - 16, y_bot - 8, 10, 4, 0x9a9284); render_rect(fb, stride, cx + 4, y_bot - 8, 12, 4, 0x9a9284);
    render_rect(fb, stride, cx + 8, y_bot - 24, 6, 20, sc->tube); render_rect(fb, stride, cx + 9, y_bot - 25, 4, 2, 0x100418);
    render_rect(fb, stride, cx + 14, y_bot - 17, 5, 13, sc->tube); render_rect(fb, stride, cx + 15, y_bot - 18, 3, 2, 0x100418);
    coral_glyph(fb, stride, cx - 9, y_bot - 4, sc->coral);
    render_rect(fb, stride, cx - 4, y_bot - 12, 12, 8, sc->brain); render_rect(fb, stride, cx - 2, y_bot - 14, 8, 2, sc->brain);
}
static void castle_glyph(uint16_t *fb, int stride, int cx, int y_bot, uint32_t wall, uint32_t roof) {
    const uint32_t tower = 0x87795f, dark = 0x0b1a22, trim = 0xc25f38;
    render_rect(fb, stride, cx - 14, y_bot - 14, 29, 15, wall);
    for (int x = cx - 14; x + 2 <= cx + 14; x += 6) render_rect(fb, stride, x, y_bot - 17, 3, 3, wall);
    render_rect(fb, stride, cx - 21, y_bot - 24, 8, 25, tower);
    for (int i = 0; i < 7; i++) render_rect(fb, stride, cx - 17 - i / 2, y_bot - 31 + i, 1 + i, 1, roof);
    render_rect(fb, stride, cx + 13, y_bot - 20, 8, 21, tower);
    for (int i = 0; i < 6; i++) render_rect(fb, stride, cx + 17 - i / 2, y_bot - 26 + i, 1 + i, 1, roof);
    render_rect(fb, stride, cx - 5, y_bot - 9, 11, 1, trim);
    render_rect(fb, stride, cx - 3, y_bot - 8, 7, 9, dark);
    render_rect(fb, stride, cx - 4, y_bot - 6, 9, 7, dark);
}
/* a little pumpkin lantern for its depth tiles (0.3.6): a squat orange
 * body in three ribs, a stem, a lit face */
static void pumpkin_glyph(uint16_t *fb, int stride, int cx, int y_bot) {
    render_rect(fb, stride, cx - 11, y_bot - 13, 22, 11, 0xe8641a);
    render_rect(fb, stride, cx - 9, y_bot - 15, 18, 15, 0xe8641a);
    render_rect(fb, stride, cx - 13, y_bot - 11, 26, 7, 0xc2420e);
    render_rect(fb, stride, cx - 12, y_bot - 12, 24, 9, 0xe8641a);
    render_rect(fb, stride, cx - 4, y_bot - 14, 1, 13, 0xa83410);    /* the ribs */
    render_rect(fb, stride, cx + 4, y_bot - 14, 1, 13, 0xa83410);
    render_rect(fb, stride, cx - 8, y_bot - 12, 3, 3, 0xf88a2c);     /* the light, upper left */
    render_rect(fb, stride, cx - 1, y_bot - 19, 3, 5, 0x66702a);     /* the stem */
    render_rect(fb, stride, cx - 7, y_bot - 10, 3, 2, 0xffd470);     /* the face, lit */
    render_rect(fb, stride, cx + 4, y_bot - 10, 3, 2, 0xffd470);
    render_rect(fb, stride, cx - 5, y_bot - 6, 10, 2, 0xffa83a);
}
/* a little volcano for its depth tiles (0.3.9): a dark cone, the lava pool
 * at the crater, a crack down the flank, the cave at the foot */
static void volcano_glyph(uint16_t *fb, int stride, int cx, int y_bot) {
    for (int i = 0; i < 24; i++) {                                   /* the cone, wider at the foot */
        int half = 3 + (23 - i) * 15 / 23;
        render_rect(fb, stride, cx - half, y_bot - i, half * 2 + 1, 1, i % 5 == 2 ? 0x362e3d : 0x52475a);
    }
    render_rect(fb, stride, cx - 4, y_bot - 24, 9, 3, 0xffa826);     /* the pool */
    render_rect(fb, stride, cx - 2, y_bot - 24, 5, 1, 0xffe070);
    render_rect(fb, stride, cx - 3, y_bot - 21, 2, 6, 0xf26a1c);     /* a crack, then another */
    render_rect(fb, stride, cx - 4, y_bot - 15, 2, 5, 0xf26a1c);
    render_rect(fb, stride, cx + 2, y_bot - 21, 2, 8, 0xf26a1c);
    render_rect(fb, stride, cx - 3, y_bot - 6, 7, 7, 0x1e0c0c);      /* the cave */
    render_rect(fb, stride, cx - 2, y_bot - 1, 5, 2, 0x9a3012);
}
/* a depth tile: the plant's - two leaves and the keeper's first fish, in
 * the order the choice means - BEHIND (the plant behind the fish): leaves,
 * then the fish over them; AMONG: one leaf, the fish, the other leaf; IN
 * FRONT: the fish, then both leaves over it. The castle's (2026-09-16):
 * the castle and two leaves - BEHIND: the castle, the leaves over it; IN
 * FRONT: the leaves, the castle over them (its depths mean the plant layer) */
static void depth_tile(const tank_t *t, uint16_t *fb, int stride, int x, int y, int w, int h, int z, int item, double clock) {
    const int cx = x + w / 2, cy = y + h / 2, lh = h - 6, yb = y + h - 3;
    const uint32_t la = 0x8dbb48, lb = 0x6c9d38;
    const fish_t *who = t->n_fish > 0 ? &t->fish[0] : NULL;
    if (item == 8) {                                    /* the pumpkin: the castle's reading, the lantern between two leaves */
        if (z == DECOR_Z_BACK) pumpkin_glyph(fb, stride, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) pumpkin_glyph(fb, stride, cx, yb);
        return;
    }
    if (item == 9) {                                    /* the volcano: the castle's reading */
        if (z == DECOR_Z_BACK) volcano_glyph(fb, stride, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) volcano_glyph(fb, stride, cx, yb);
        return;
    }
    if (item == 2) {
        if (z == DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        return;
    }
    if (item == 4) {                                    /* the cluster: the castle's reading - the plants before it, or it before them (0.3.9.4,
                                                           Strato: no fish in a piece's tiles - the fish choose their own depth now) */
        const cluster_scheme_t *sc = &CLUSTER_SCHEMES[tank_cluster_scheme(t)];
        if (z == DECOR_Z_BACK) cluster_glyph(fb, stride, cx, yb, sc);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) cluster_glyph(fb, stride, cx, yb, sc);
        return;
    }
    if (item == 3) {                                    /* the coral: likewise */
        uint32_t rgb = tank_coral_rgb(t);
        if (z == DECOR_Z_BACK) coral_glyph(fb, stride, cx, yb, rgb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) coral_glyph(fb, stride, cx, yb, rgb);
        return;
    }
    /* the sword plant (0.3.9.4, Strato: "the sword plant should not be an outlier and should have the same layer stacking UI as
       the other decorations"): its broad leaves read against the seagrass, as the castle's against the plants - not against a
       fish, which chooses its own depth now (docs/swim-depth.md) */
    (void)who; (void)cy; (void)clock;
    const uint32_t sg = 0x3f8f5a;
    if (z == DECOR_Z_BACK) { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
    for (int k = -2; k <= 2; k++) frond_glyph(fb, stride, cx + k * 9 - 1, yb, lh - 4 - (k & 1) * 6, sg);
    if (z != DECOR_Z_BACK) { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
}
/* THE ORDER SQUARES (0.4, Strato 11-02: "it looks odd with no examples when there is something like 'behind or in front of x' ...
   a universal object in front / behind squares"): the page's piece against its neighbor in the stack, drawn as ONE universal
   object for every piece - a plain block, gray with a lighter top and a dark rim, read by its shape and value, never its hue.
   BEHIND: the piece's own glyph (the plant's two broad leaves, the pumpkin, ...) and the block over it; IN FRONT: the glyph over
   the block. The real piece is named on the hint line under the squares */
static void block_glyph(uint16_t *fb, int stride, int cx, int y_bot, int w, int h) {
    const int x0 = cx - w / 2, y0 = y_bot - h + 1;
    render_rect(fb, stride, x0 + 1, y0, w - 2, h, 0x1c2430);                      /* the rim, its corners cut */
    render_rect(fb, stride, x0, y0 + 1, w, h - 2, 0x1c2430);
    render_rect(fb, stride, x0 + 2, y0 + 1, w - 4, h - 2, 0x8a919c);              /* the face */
    render_rect(fb, stride, x0 + 1, y0 + 2, w - 2, h - 4, 0x8a919c);
    render_rect(fb, stride, x0 + 2, y0 + 2, w - 4, 3, 0xc9ced6);                  /* its lit top */
    render_rect(fb, stride, x0 + 2, y_bot - 3, w - 4, 2, 0x5f6670);               /* its shaded foot */
}
static void order_piece_glyph(const tank_t *t, uint16_t *fb, int stride, int item, int cx, int yb, int lh) {
    if (item == 8) pumpkin_glyph(fb, stride, cx, yb);
    else if (item == 9) volcano_glyph(fb, stride, cx, yb);
    else if (item == 2) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
    else if (item == 4) cluster_glyph(fb, stride, cx, yb, &CLUSTER_SCHEMES[tank_cluster_scheme(t)]);
    else if (item == 3) coral_glyph(fb, stride, cx, yb, tank_coral_rgb(t));
    else { leaf_glyph(fb, stride, cx - 6, yb, lh, 0x8dbb48); leaf_glyph(fb, stride, cx + 6, yb, lh, 0x6c9d38); }   /* the sword plant */
}
/* one tile: the page's piece behind (front = false) or in front of the block */
static void order_tile(const tank_t *t, uint16_t *fb, int stride, int x, int y, int w, int h, int item, bool front) {
    const int cx = x + w / 2, yb = y + h - 3, lh = h - 6;
    const int bw = item == 0 ? 36 : 30, bh = item == 0 ? 20 : 16;
    const int bx = cx + (item == 0 ? 0 : 9), px = cx - (item == 0 ? 0 : 7);          /* (a piece glyph and the block side by side, overlapping) */
    if (front) { block_glyph(fb, stride, bx, yb, bw, bh); order_piece_glyph(t, fb, stride, item, px, yb, lh); }
    else { order_piece_glyph(t, fb, stride, item, px, yb, lh); block_glyph(fb, stride, bx, yb, bw, bh); }
}
/* the squares' piece (Strato 11-02, the plant's page): with several pieces they toggle the plant against ONE - the one it last
   passed on this page (an arrow's step or a square's), at first the one just in front of it, else just behind; the arrows walk
   the whole stack and the squares follow, so IN FRONT never lights another piece's BEHIND. -1 = no piece beside it */
static int order_ref(const tank_t *t) {
    const int below = tank_decor_neighbor(t, s_item, -1), above = tank_decor_neighbor(t, s_item, 1);
    if (s_ref >= 0 && (s_ref == below || s_ref == above)) return s_ref;
    return above >= 0 ? above : below;
}
/* the two squares: BEHIND | IN FRONT of `ref`, the lit one the page's piece's place; the hint line under them names ref ("BEHIND
   THE PUMPKIN" - Strato 11-02: no caption over them too, it said the same) */
static void order_bar(const tank_t *t, uint16_t *fb, int stride, int bx, int by, int segw, int bh, int tile_h, int item, int ref) {
    const bool front = tank_decor_above(t, item, ref);
    render_rect(fb, stride, bx, by, 2 * segw, bh, C_KEY);
    static const char *const W[2] = { "BEHIND", "IN FRONT" };
    for (int i = 0; i < 2; i++) {
        const int sx = bx + i * segw; const bool on = (i == 1) == front;
        if (on) { render_rect(fb, stride, sx, by, segw, bh, C_INNER); render_rect_edge(fb, stride, sx + 1, by + 1, segw - 2, bh - 2, C_EDGE); }
        else if (i > 0) render_rect(fb, stride, sx, by + 6, 1, bh - 12, C_DIM);
        order_tile(t, fb, stride, sx, by + 4, segw, tile_h, item, i == 1);
        text_c(fb, stride, sx + segw / 2, by + tile_h + 10, 2, on ? C_TEXT : dim(C_CAPT, 45), W[i]);
    }
    render_rect_edge(fb, stride, bx, by, 2 * segw, bh, C_EDGE);
}
/* an order arrow (the placement page): a chevron in a small button, lit or dim */
static void ord_arrow(uint16_t *fb, int stride, int x, int y, bool right, bool on) {
    uint32_t e = on ? C_EDGE : dim(C_CAPT, 45);
    render_button(fb, stride, x, y, SETUP_ORD_W, SETUP_BTN_H, C_INNER, e, "", 2);
    int cx = x + SETUP_ORD_W / 2, cy = y + SETUP_BTN_H / 2;
    for (int i = 0; i < 7; i++) {
        int xx = right ? cx - 3 + i : cx + 3 - i;
        render_rect(fb, stride, xx, cy - 6 + i, 2, 1, e);
        render_rect(fb, stride, xx, cy + 6 - i, 2, 1, e);
    }
}
static void panel(uint16_t *fb, int stride) {
    render_rect(fb, stride, SETUP_X, SETUP_Y, SETUP_W, SETUP_H, C_PANEL);
    render_rect_edge(fb, stride, SETUP_X, SETUP_Y, SETUP_W, SETUP_H, C_EDGE);
    render_rect_edge(fb, stride, SETUP_X + 1, SETUP_Y + 1, SETUP_W - 2, SETUP_H - 2, C_INNER);
}

void render_setup(const tank_t *t, uint16_t *fb, int stride, double clock) {
    if (s_same_note > 0) s_same_note--;
    if (!s_active) return;
    const int CX = PAGE_W / 2;
    const int FLOOR = TANK_BOT - 16 - PAGE_Y;           /* the tank's sand line, in the page's coordinates */
    if (s_page == SETUP_PG_WELCOME) {
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 36, 3, C_TEXT, "WELCOME");
        static const char *const ls[] = {
            "TWO FRY HAVE MOVED IN.",
            "THEY EAT, PLAY, REST AND",
            "GROW UP WHILE YOU WATCH.",
            "EACH ONE THINKS FOR ITSELF.",
            "FIRST, LET'S MEET THEM.",
        };
        lines_c(fb, stride, SETUP_Y + 96, 24, C_CAPT, ls, 5);
        render_button(fb, stride, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_INNER, C_EDGE, "NEXT", 2);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (s_page == SETUP_PG_BUBBLES) {
        /* the live tank with a stripe down the column: drag it anywhere */
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, "PLACE THE BUBBLES");
        nav(fb, stride, true, "NEXT", false);
        int bx = (int)t->bubble_x - PAGE_X;
        render_rect_blend(fb, stride, bx - 22, SETUP_TOP_BTN_Y + SETUP_BTN_H + 10, 44, FLOOR - (SETUP_TOP_BTN_Y + SETUP_BTN_H + 10), C_EDGE, 46);
        chevron(fb, stride, bx, FLOOR - 34, true, C_EDGE);
        text_c(fb, stride, CX, 296, 2, C_CAPT, "DRAG THEM LEFT OR RIGHT");
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_PLACE) {
        /* the live tank with a stripe over the piece's footprint: drag it
           anywhere on the water; the layer row picks its depth (the tank
           under the page redraws with it, so the fish and grass show the choice) */
        char title[40]; snprintf(title, sizeof title, "PLACE THE %s", SD_ITEMS[s_item].name);
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, title);
        render_button(fb, stride, SETUP_TOP_DONE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "DONE", 2);
        /* SELL, top left (2026-09-24): a tap arms it and names the refund, a
           second tap sells the piece back to the shop and closes the page */
        if (s_sell_armed) { char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_sell_value(s_item));
                            render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, ok, 2); }
        else render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_INNER, C_DIM, "SELL", 2);
        /* SHELVE beside it (0.3.9): put the piece away for later, kept as it is */
        render_button(fb, stride, SETUP_TOP_SHELVE_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_INNER, C_EDGE, "SHELVE", 2);
        /* LOCK between SHELVE and DONE (0.3.9): the word says the state - LOCK
           (open, an outlined button) or LOCKED (shut, a filled one) - never the
           color alone (Strato is color blind) */
        if (tank_decor_locked(t, s_item)) render_button(fb, stride, SETUP_TOP_LOCK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_FILL, C_TEXT, "LOCKED", 2);
        else render_button(fb, stride, SETUP_TOP_LOCK_X, SETUP_TOP_BTN_Y, SETUP_PL_BTN_W, SETUP_BTN_H, C_INNER, C_EDGE, "LOCK", 2);
        /* the ORDER arrows (2026-10-17): a step back / forward in the stack of
           pieces at this depth, lit when there is a piece to pass; the relation
           is named on the hint line ("UNDER THE CORAL"). At the ends of the
           DEPTH row since 0.3.9; the plant (no stack, three tiles) has none */
        int below = tank_decor_neighbor(t, s_item, -1), above = tank_decor_neighbor(t, s_item, 1);
        ord_arrow(fb, stride, SETUP_ORD_X0, SETUP_ORD_Y, false, below >= 0);   /* (the sword plant too since 0.3.9.4: a piece in the stack like the rest) */
        ord_arrow(fb, stride, SETUP_ORD_X1, SETUP_ORD_Y, true, above >= 0);
        /* the relation, in the depth bar's own words (Strato 2026-10-17: "behind
           the coral" / "in front of the coral"); "THE" goes when the line is too
           long for where it sits (the hint line, or beside COLOR / LOOK) */
        char rel[40] = "";
        int rel_avail = PAGE_W - 2 * 56 - 8;                               /* the hint line: the box every glass shows whole */
        if (s_item == 3) rel_avail = (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W - render_text_w("COLOR", 2) - 14;
        if (s_item == 4) rel_avail = (SETUP_LOOK_N - 1) * SETUP_LOOK_PX + SETUP_LOOK_W - render_text_w("LOOK", 2) - 14;
        for (int pass = 0; pass < 2; pass++) {
            const char *the = pass ? "" : "THE ";
            if (above >= 0) snprintf(rel, sizeof rel, "BEHIND %s%s", the, SD_ITEMS[above].name);
            else if (below >= 0) snprintf(rel, sizeof rel, "IN FRONT OF %s%s", the, SD_ITEMS[below].name);
            if (render_text_w(rel, 2) <= rel_avail) break;
        }
        /* the DEPTH bar: one outlined box, three joined segments, the chosen
           one lit; a picture tile on each and the word under it */
        static const char *const zl[DECOR_Z_N] = { "BEHIND", "AMONG", "IN FRONT" };
        static const char *const hint[DECOR_Z_N] = { "IN FRONT OF THE SEAGRASS", "", "IN FRONT OF THE SEAGRASS" };   /* (the sword plant:
                                                                   one depth since 0.4, over the seagrass - the fish choose their own) */
        static const char *const castle_hint[DECOR_Z_N] = { "THE PLANTS GROW IN FRONT OF IT", "", "IT STANDS IN FRONT OF THE PLANTS" };
        static const char *const coral_hint[DECOR_Z_N] = { "BEHIND THE GRASS AND THE FISH", "", "IN FRONT OF EVERYTHING" };
        int z = tank_decor_z(t, s_item), zi = tank_decor_z_index(s_item, z);
        int n = tank_decor_z_count(s_item), bw = n * SETUP_DEPTH_SEG_W, bx = (PAGE_W - bw) / 2;   /* the item's own depths */
        if (n > 1) {                                                       /* (the plant has one depth: no bar, 0.4) */
        text_c(fb, stride, CX, SETUP_DEPTH_Y - 18, 2, C_CAPT, "DEPTH");
        render_rect(fb, stride, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, C_KEY);
        for (int i = 0; i < n; i++) {
            int sx = bx + i * SETUP_DEPTH_SEG_W, zi_ = tank_decor_z_at(s_item, i);
            if (i == zi) { render_rect(fb, stride, sx, SETUP_DEPTH_Y, SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, C_INNER);
                           render_rect_edge(fb, stride, sx + 1, SETUP_DEPTH_Y + 1, SETUP_DEPTH_SEG_W - 2, SETUP_DEPTH_H - 2, C_EDGE); }
            else if (i > 0) render_rect(fb, stride, sx, SETUP_DEPTH_Y + 6, 1, SETUP_DEPTH_H - 12, C_DIM);   /* a divider */
            depth_tile(t, fb, stride, sx, SETUP_DEPTH_Y + 4, SETUP_DEPTH_SEG_W, SETUP_DEPTH_TILE_H, zi_, s_item, clock);
            text_c(fb, stride, sx + SETUP_DEPTH_SEG_W / 2, SETUP_DEPTH_Y + SETUP_DEPTH_TILE_H + 10, 2, i == zi ? C_TEXT : dim(C_CAPT, 45), zl[zi_]);
        }
        render_rect_edge(fb, stride, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, C_EDGE);
        }
        const int ref = n < 2 ? order_ref(t) : -1;          /* THE ORDER SQUARES (the plant, one depth): in the DEPTH bar's place, the arrows at its ends;
                                                               the hint line names their piece */
        if (ref >= 0) {
            order_bar(t, fb, stride, SETUP_ORDSQ_X, SETUP_DEPTH_Y, SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, SETUP_DEPTH_TILE_H, s_item, ref);
            for (int pass = 0; pass < 2; pass++) {
                const char *the = pass ? "" : "THE ";
                if (tank_decor_above(t, ref, s_item)) snprintf(rel, sizeof rel, "BEHIND %s%s", the, SD_ITEMS[ref].name);
                else snprintf(rel, sizeof rel, "IN FRONT OF %s%s", the, SD_ITEMS[ref].name);
                if (render_text_w(rel, 2) <= rel_avail) break;
            }
        }
        if (s_item == 4) {
            /* the cluster: a LOOK row of three tiles - the coral, the tubes and
               the brain as three bands over the look's name - the pick ringed */
            int look = tank_cluster_scheme(t);
            render_rect_blend(fb, stride, -PAGE_X, SETUP_LOOK_Y - 22, TANK_W, SETUP_LOOK_H + 30, C_PANEL, 110);
            render_text(fb, stride, SETUP_LOOK_X + 2, SETUP_LOOK_Y - 18, 2, C_CAPT, "LOOK");
            if (rel[0]) render_text(fb, stride, SETUP_LOOK_X + SETUP_LOOK_N * SETUP_LOOK_PX - SETUP_LOOK_PX + SETUP_LOOK_W - 2 - render_text_w(rel, 2), SETUP_LOOK_Y - 18, 2, C_CAPT, rel);
            for (int i = 0; i < SETUP_LOOK_N; i++) {
                int x = SETUP_LOOK_X + i * SETUP_LOOK_PX; bool on = i == look;
                const cluster_scheme_t *sc = &CLUSTER_SCHEMES[i];
                render_rect(fb, stride, x, SETUP_LOOK_Y, SETUP_LOOK_W, SETUP_LOOK_H, C_KEY);
                int bw = (SETUP_LOOK_W - 6) / 3;
                render_rect(fb, stride, x + 3, SETUP_LOOK_Y + 3, bw, 14, sc->coral);
                render_rect(fb, stride, x + 3 + bw, SETUP_LOOK_Y + 3, bw, 14, sc->tube);
                render_rect(fb, stride, x + 3 + 2 * bw, SETUP_LOOK_Y + 3, SETUP_LOOK_W - 6 - 2 * bw, 14, sc->brain);
                text_c(fb, stride, x + SETUP_LOOK_W / 2, SETUP_LOOK_Y + 20, 2, on ? C_TEXT : dim(C_CAPT, 45), sc->name);
                render_rect_edge(fb, stride, x, SETUP_LOOK_Y, SETUP_LOOK_W, SETUP_LOOK_H, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, SETUP_LOOK_Y + 1, SETUP_LOOK_W - 2, SETUP_LOOK_H - 2, C_TEXT);
            }
        } else if (s_item == 3) {
            /* the coral: no hint line - a COLOR row instead (the fish colour
               page's swatches, shorter), the pick ringed, the hint under the
               row's caption */
            uint32_t rgb = tank_coral_rgb(t);
            render_rect_blend(fb, stride, -PAGE_X, SETUP_COL_Y - 22, TANK_W, SETUP_COL_H + 30, C_PANEL, 110);   /* a band the swatches read on, like the colour page's */
            render_text(fb, stride, SETUP_COL_X + 2, SETUP_COL_Y - 18, 2, C_CAPT, "COLOR");
            if (rel[0]) render_text(fb, stride, SETUP_COL_X + (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W - 2 - render_text_w(rel, 2), SETUP_COL_Y - 18, 2, C_CAPT, rel);
            (void)coral_hint;
            for (int i = 0; i < SETUP_COL_N; i++) {
                int x = SETUP_COL_X + i * SETUP_COL_PX;
                bool on = CORAL_PAL[i] == rgb;
                render_rect(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, CORAL_PAL[i]);
                render_rect_edge(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, SETUP_COL_Y + 1, SETUP_COL_W - 2, SETUP_COL_H - 2, C_TEXT);
            }
        } else text_c(fb, stride, CX, SETUP_DEPTH_HINT_Y, 2, C_CAPT, rel[0] ? rel : s_item == 2 || s_item == 8 || s_item == 9 ? castle_hint[z] : hint[z]);
        float x0 = tank_decor_x(t, s_item) - tank_decor_half_w(s_item) - 10, x1 = tank_decor_x(t, s_item) + tank_decor_half_w(s_item) + 10, top = TANK_BOT - 16 - 40;
        if (s_item == 0) tank_veg_bed(t, 3, NULL, NULL, &top, NULL);   /* the leaves' reach */
        else top = tank_decor_fy(t, s_item) - tank_decor_height(s_item);   /* its highest pixel on its depth's floor line (0.3.9) */
        x0 -= PAGE_X; x1 -= PAGE_X; top -= PAGE_Y;                     /* the piece is in the tank; the stripe is drawn on the page */
        int sy = (int)top - 8; if (sy < place_y()) sy = place_y();
        render_rect_blend(fb, stride, (int)x0, sy, (int)(x1 - x0), FLOOR - sy + 4, C_EDGE, 46);
        chevron(fb, stride, (int)((x0 + x1) * 0.5f), FLOOR - 34, true, C_EDGE);
        text_c(fb, stride, CX, 296, 2, C_CAPT, "DRAG IT LEFT OR RIGHT");
    } else if (page_is_name() && s_kbd) {
        /* the two rejected designs, drawn as they were: a panel over the tank,
           the name as underlined slots (the next free one blinks; an untouched
           default sits dimmed, as a placeholder does), the keys */
        const fish_t *f = &t->fish[page_fish()];
        bool grid = s_kbd == SETUP_KBD_GRID;
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + (grid ? 12 : 7), 2, C_CAPT, s_rename ? "RENAME YOUR FISH" : s_birth ? "NAME THE NEW FRY" : page_fish() == 0 ? "NAME THE FIRST FISH" : "NAME THE SECOND FISH");
        if (grid) render_fish_preview(fb, stride, SETUP_X + 88, SETUP_Y + 72, 2.0f, f->color, f->fin, f->accent, f, FISH_ROOM_CARD, clock);
        const int SL = 22, NX = grid ? SETUP_X + 160 : CX - FISH_NAME_MAX * SL / 2 + 2, NY = grid ? SETUP_Y + 58 : SETUP_TOP_BTN_Y + (SETUP_BTN_H - 28) / 2;
        int n = name_len(f);
        for (int i = 0; i < FISH_NAME_MAX; i++) {
            int x = NX + i * SL;
            bool cursor = i == n && ((int)(clock * 2) & 1);
            render_rect(fb, stride, x, NY + 26, SL - 4, 2, cursor ? C_EDGE : i < n ? name_rgb(f->color) : C_DIM);
            if (i < n) { char ch[2] = { f->name[i], 0 }; render_text(fb, stride, x + 3, NY, 3, s_typed ? C_TEXT : C_CAPT, ch); }
        }
        if (grid) for (int k = 0; k <= SETUP_KEY_DEL; k++) {
            int x = KG_X + kg_col(k) * KG_PX, y = KG_Y + kg_row(k) * KG_PY, w = kg_w(k);
            render_rect(fb, stride, x, y, w, KG_H, C_KEY);
            render_rect_edge(fb, stride, x, y, w, KG_H, C_DIM);
            char lab[4] = { (char)('A' + k), 0 };
            if (k == SETUP_KEY_DEL) strcpy(lab, "DEL");
            render_text(fb, stride, x + (w - render_text_w(lab, 2)) / 2, y + (KG_H - 14) / 2, 2, C_TEXT, lab);
        } else for (int cell = 0; cell < KP_COLS * KP_ROWS; cell++) {
            int k = kp_key(cell), x = KP_X + (cell % KP_COLS) * KP_PX, y = KP_Y + (cell / KP_COLS) * KP_PY;
            bool special = k >= 26;
            render_rect(fb, stride, x, y, KP_W, KP_H, special ? C_INNER : C_KEY);
            render_rect_edge(fb, stride, x, y, KP_W, KP_H, special ? C_EDGE : C_DIM);
            char lab[4] = { (char)('A' + k), 0 };
            if (k == SETUP_KEY_DEL) strcpy(lab, "DEL");
            if (k == SETUP_KEY_PAGE) strcpy(lab, s_kb_page ? "A-M" : "N-Z");
            render_text(fb, stride, x + (KP_W - render_text_w(lab, 3)) / 2, y + (KP_H - 21) / 2, 3, C_TEXT, lab);
        }
        nav(fb, stride, !grid, s_rename ? "DONE" : "NEXT", s_rename);
        if (grid) dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (page_is_name()) {
        /* straight on the tank: the fish being named wears a ring in its own
           colour, its name spans the middle in that colour, the active slot
           bright with the chevrons, the rest dimmed; slots past the first
           blank are just faint underlines */
        const fish_t *f = &t->fish[page_fish()];
        fish_ring(fb, stride, f, 17 * f->size + 6, f->color);
        if (name_beside(t)) {                                   /* the form they chose, in view while they name it (0.4) */
            render_rect_blend(fb, stride, SETUP_ED_GROWN_X - 64, SETUP_NAME_PREVIEW_Y - 24, 128, 48, C_PANEL, 150);
            render_fish_preview(fb, stride, SETUP_ED_GROWN_X, SETUP_NAME_PREVIEW_Y, SETUP_NAME_GROWN_SIZE, f->color, f->fin, f->accent, f, FISH_ROOM_FAM, clock);
            text_c(fb, stride, (SETUP_ED_NOW_X + SETUP_ED_GROWN_X) / 2, SETUP_NAME_PREVIEW_Y - 10, 3, C_CAPT, ">");
        }
        render_rect_blend(fb, stride, -PAGE_X, SETUP_SLOT_Y - SETUP_ARROW_GAP - 2, TANK_W, SETUP_SLOT_H + 2 * SETUP_ARROW_GAP + 36, C_PANEL, 150);
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, s_rename ? "RENAME YOUR FISH" : s_birth ? "NAME THE NEW FRY" : page_fish() == 0 ? "NAME THE FIRST FISH" : "NAME THE SECOND FISH");
        nav(fb, stride, true, s_rename ? "DONE" : "NEXT", s_rename);
        int n = name_len(f);
        if (s_slot > n) s_slot = n;
        for (int i = 0; i < FISH_NAME_MAX; i++) {
            int x = SETUP_SLOT_X + i * SETUP_SLOT_PX;
            bool on = i == s_slot, reach = i <= n;
            uint32_t col = on ? name_rgb(f->color) : reach ? dim(name_rgb(f->color), 55) : C_DIM;
            int v = slot_val(f, i);
            if (v) { char ch[2] = { (char)('A' + v - 1), 0 }; render_text(fb, stride, x, SETUP_SLOT_Y, SETUP_SLOT_SCALE, col, ch); }
            render_rect(fb, stride, x, SETUP_SLOT_Y + SETUP_SLOT_H + 8, SETUP_SLOT_W, 4,
                        on && !v && ((int)(clock * 2) & 1) ? C_TEXT : col);
            if (on) {
                chevron(fb, stride, x + SETUP_SLOT_W / 2, SETUP_SLOT_Y - SETUP_ARROW_GAP, true, C_EDGE);
                chevron(fb, stride, x + SETUP_SLOT_W / 2, SETUP_SLOT_Y + SETUP_SLOT_H + SETUP_ARROW_GAP + 4, false, C_EDGE);
            }
        }
        text_c(fb, stride, CX, SETUP_SLOT_Y + SETUP_SLOT_H + 96, 2, C_CAPT, "SWIPE A LETTER UP OR DOWN");
        dots(fb, stride, PAGE_H - 14);
    } else if (page_is_look()) {
        /* THE EDITOR (setup.h): straight on the tank, the fry ringed on stage
           (NOW), the grown fish on a dark plate (GROWN UP), the rows under a
           panel band */
        const fish_t *f = &t->fish[page_fish()];
        char cap[40]; snprintf(cap, sizeof cap, "%s FOR THE %s FISH", page_is_form() ? "A FORM" : "COLORS", page_fish() ? "SECOND" : "FIRST");   /* not named yet (0.4) */
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, cap);
        nav(fb, stride, true, "NEXT", false);
        render_button(fb, stride, SETUP_ROLL_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_KEY, C_DIM, "ROLL", 2);
        const edit_row_t *r; int n = edit_rows(&r);
        int band = SETUP_ED_CAPTION_Y - 6;                       /* from the captions down: fish swim behind them */
        render_rect_blend(fb, stride, -PAGE_X, band, TANK_W, r[n - 1].y + r[n - 1].h + 10 - band, C_PANEL, 110);
        fish_ring(fb, stride, f, 17 * f->size + 6, f->color);
        render_rect_blend(fb, stride, SETUP_ED_GROWN_X - 72, SETUP_ED_PREVIEW_Y - 26, 144, 54, C_PANEL, 150);   /* clear of the buttons */
        render_fish_preview(fb, stride, SETUP_ED_GROWN_X, SETUP_ED_PREVIEW_Y, SETUP_ED_GROWN_SIZE, f->color, f->fin, f->accent, f, FISH_ROOM_CARD, clock);
        text_c(fb, stride, (SETUP_ED_NOW_X + SETUP_ED_GROWN_X) / 2, SETUP_ED_PREVIEW_Y - 10, 3, C_CAPT, ">");
        text_c(fb, stride, SETUP_ED_NOW_X, SETUP_ED_CAPTION_Y, 2, C_CAPT, "NOW");
        text_c(fb, stride, SETUP_ED_GROWN_X, SETUP_ED_CAPTION_Y, 2, C_CAPT, "GROWN UP");
        for (int k = 0; k < n; k++) {
            int cur = tank_gene(f, r[k].gene);
            char lab[32];
            if (r[k].gene == GENE_PATTERN) snprintf(lab, sizeof lab, "%s  %s", r[k].label, PATTERN_NAMES[cur]);   /* its tiles carry no names */
            else if (r[k].gene == GENE_ACCENT && s_same_note > 0) snprintf(lab, sizeof lab, "%s  SAME AS ITS BODY", r[k].label);
            else snprintf(lab, sizeof lab, "%s", r[k].label);
            bool unused = r[k].gene == GENE_ACCENT && f->pattern == PATTERN_SOLID;   /* a solid fish wears no markings */
            render_text(fb, stride, r[k].x + 2, r[k].y - SETUP_LABEL_DY, 2, unused ? dim(C_CAPT, 45) : C_CAPT, lab);
            for (int i = 0; i < r[k].n; i++) {
                int x = r[k].x + i * r[k].px, y = r[k].y, w = r[k].w, h = r[k].h;
                bool on = i == cur;
                if (r[k].gene == GENE_BODY || r[k].gene == GENE_ACCENT) {
                    uint32_t rgb = r[k].gene == GENE_BODY ? LOOK_BODY[i] : LOOK_ACCENT[i];
                    bool off = r[k].gene == GENE_ACCENT && rgb == f->color;   /* markings the body's own color would vanish */
                    render_rect(fb, stride, x, y, w, h, rgb);                 /* in its own color, so it can be told (Strato
                                                                                 10-04: dimmed, it was a mystery color) */
                    render_rect_edge(fb, stride, x, y, w, h, on ? C_TEXT : C_DIM);
                    if (on) render_rect_edge(fb, stride, x + 1, y + 1, w - 2, h - 2, C_TEXT);
                    if (off) struck_out(fb, stride, x, y, w, h, rgb);              /* a shape says "not this one", never a hue */
                    continue;
                }
                /* a tile: a small grown fish of that option, in this fish's colors */
                fish_t form = *f;
                if (r[k].gene == GENE_SHAPE) form.shape = (uint8_t)i;
                else if (r[k].gene == GENE_FINS) form.fins = (uint8_t)i;
                else form.pattern = (uint8_t)i;
                render_rect(fb, stride, x, y, w, h, C_KEY);
                render_rect_edge(fb, stride, x, y, w, h, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, y + 1, w - 2, h - 2, C_TEXT);
                if (r[k].gene == GENE_PATTERN)
                    render_fish_preview(fb, stride, x + w / 2.0f, y + h / 2.0f, 1.4f, f->color, f->fin, f->accent, &form, FISH_ROOM_ROW, clock);
                else {
                    render_fish_preview(fb, stride, x + w / 2.0f, y + 21, 1.0f, f->color, f->fin, f->accent, &form, FISH_ROOM_ROW, clock);
                    text_c(fb, stride, x + w / 2, y + h - 17, 2, on ? C_TEXT : dim(C_CAPT, 60),
                           r[k].gene == GENE_SHAPE ? SHAPE_NAMES[i] : FINS_NAMES[i]);
                }
            }
            if (unused)                                     /* a solid fish: the markings row stands back (still a pick away -
                                                               choosing a pattern brings them back) */
                render_rect_blend(fb, stride, r[k].x - 2, r[k].y - 2, (r[k].n - 1) * r[k].px + r[k].w + 4, r[k].h + 4, C_PANEL, 150);
        }
        if (page_is_colors()) swap_button(fb, stride);
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_BORN) {
        /* the announcement, over the live tank: the fry wears a double ring
           wherever it hatched (no stage - it stays in the grass it was born
           in), its parents are named, one button leads on */
        const fish_t *f = &t->fish[page_fish()];
        char news[40]; bool mut = mutation_words(f, news, sizeof news);
        float r = 17 * f->size + 6 + 3 * sinf(tank_anim(clock) * 3);   /* a ring that breathes */
        fish_ring(fb, stride, f, r, f->color);
        fish_ring(fb, stride, f, r + 8, mut ? C_NEW : C_EDGE);     /* a mutation: a gold outer ring */
        render_rect_blend(fb, stride, -PAGE_X, 92, TANK_W, mut ? 138 : 116, C_PANEL, 150);
        text_c(fb, stride, CX, 104, 3, C_TEXT, "A NEW FRY!");
        char l1[FISH_NAME_MAX * 2 + 24];
        bool shop = f->parent_a < 0 && f->parent_b < 0;              /* bought (0.3.5): no parents, in at the surface */
        if (shop) snprintf(l1, sizeof l1, "FROM THE SHOP,");
        else snprintf(l1, sizeof l1, "BORN TO %s AND %s", parent_of(t, f, 0).name, parent_of(t, f, 1).name);
        text_c(fb, stride, CX, 140, 2, C_CAPT, l1);
        text_c(fb, stride, CX, 162, 2, C_CAPT, shop ? "IN AT THE SURFACE." : "DOWN IN THE GRASS.");
        text_c(fb, stride, CX, 184, 2, C_CAPT, "GO AND SAY HELLO.");
        if (mut) text_c(fb, stride, CX, 206, 2, C_NEW, news);      /* 0.4: neither parent shows it */
        render_button(fb, stride, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "MEET IT", 2);
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_FAMILY) {
        /* the family page (setup.h): NOW and GROWN UP, then each gene and
           where it came from - a parent (* = that parent's hidden copy), or
           NEW! (a mutation) */
        const fish_t *f = &t->fish[page_fish()];
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 20, 3, C_TEXT, f->name);
        float ps = f->size * 2.0f;                                   /* the fry, twice life size ... */
        if (ps > 1.2f) ps = 1.2f;                                    /* ... a juvenile's ring clear of the name */
        render_ring(fb, stride, SETUP_FAM_NOW_X, SETUP_FAM_PORTRAIT_Y, 17 * ps + 6, f->color);
        render_fish_portrait(fb, stride, SETUP_FAM_NOW_X, SETUP_FAM_PORTRAIT_Y, ps, f, FISH_ROOM_FAM, clock);
        text_c(fb, stride, (SETUP_FAM_NOW_X + SETUP_FAM_GROWN_X) / 2 - 6, SETUP_FAM_PORTRAIT_Y - 10, 3, C_CAPT, ">");
        render_fish_preview(fb, stride, SETUP_FAM_GROWN_X, SETUP_FAM_PORTRAIT_Y, SETUP_FAM_GROWN_SIZE, f->color, f->fin, f->accent, f, FISH_ROOM_FAM, clock);
        render_text(fb, stride, SETUP_FAM_FROM_X, SETUP_FAM_ROW_Y - 20, 2, dim(C_CAPT, 60), "FROM");
        bool hidden = false;
        for (int g = 0; g < GENE_N; g++) {
            int y = SETUP_FAM_ROW_Y + g * SETUP_FAM_ROW_DY, vx = SETUP_FAM_VALUE_X;
            static const char *const LABEL[GENE_N] = { "SHAPE", "FINS", "PATTERN", "BODY", "MARKINGS" };
            render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, LABEL[g]);
            if (g == GENE_BODY || g == GENE_ACCENT) {
                uint32_t rgb = g == GENE_BODY ? f->color : f->accent;
                render_rect(fb, stride, vx, y - 1, 16, 16, rgb); render_rect_edge(fb, stride, vx, y - 1, 16, 16, C_INNER);
            } else render_text(fb, stride, vx, y, 2, C_TEXT, g == GENE_SHAPE ? SHAPE_NAMES[f->shape] : g == GENE_FINS ? FINS_NAMES[f->fins] : PATTERN_NAMES[f->pattern]);
            if (f->mutated == g + 1) { render_text(fb, stride, SETUP_FAM_FROM_X, y, 2, C_NEW, "NEW!"); continue; }
            parent_t p = parent_of(t, f, (f->from_b >> g & 1) ? 1 : 0);
            bool sec = f->from_carried >> g & 1;
            hidden |= sec;
            text_named(fb, stride, SETUP_FAM_FROM_X, y, "", p.name, p.known ? p.color : C_DIM, sec ? " *" : NULL);
        }
        if (hidden) text_c(fb, stride, CX, SETUP_FAM_NOTE_Y, 2, dim(C_CAPT, 70), "* HIDDEN IN THAT PARENT");
        nav(fb, stride, false, "NEXT", false);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (s_page == SETUP_PG_NATURE) {
        /* the nature page: how bold and sociable it is next to its parents'
           marks, and who they are */
        const fish_t *f = &t->fish[page_fish()];
        parent_t PA = parent_of(t, f, 0), PB = parent_of(t, f, 1);
        const parent_t *pa = PA.known ? &PA : NULL, *pb = PB.known ? &PB : NULL;
        bool shop = f->parent_a < 0 && f->parent_b < 0;              /* bought (0.3.5): its look is the shop's */
        const char *na = shop ? "THE SHOP" : PA.name, *nb = shop ? "THE SHOP" : PB.name;
        uint32_t ca = pa ? name_rgb(pa->color) : shop ? C_CAPT : C_DIM, cb = pb ? name_rgb(pb->color) : shop ? C_CAPT : C_DIM;
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 20, 3, C_TEXT, f->name);
        text_c(fb, stride, CX, SETUP_Y + 56, 2, C_CAPT, "ITS NATURE");
        int y = SETUP_NAT_ROW_Y, vx = SETUP_FAM_VALUE_X + 12;
        static const char *const TRAIT[2] = { "BOLD", "SOCIAL" };
        for (int k = 0; k < 2; k++, y += SETUP_NAT_ROW_DY) {          /* the fry's own bar, the parents' ticks */
            float v = k ? f->sociable : f->bold;
            float va = pa ? (k ? pa->sociable : pa->bold) : -1, vb = pb ? (k ? pb->sociable : pb->bold) : -1;
            int by = y + 3;
            render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, TRAIT[k]);
            render_rect(fb, stride, vx, by, SETUP_FAM_BAR_W, SETUP_FAM_BAR_H, C_DIM);
            render_rect(fb, stride, vx, by, (int)(SETUP_FAM_BAR_W * v + 0.5f), SETUP_FAM_BAR_H, C_FILL);
            for (int p = 0; p < 2; p++) {                            /* a parent's tick: its body colour on an ink outline, so it reads on the fill too */
                float vp = p ? vb : va; if (vp < 0) continue;
                int tx = vx + (int)(SETUP_FAM_BAR_W * vp);
                render_rect(fb, stride, tx - 2, by - 5, 5, SETUP_FAM_BAR_H + 10, C_PANEL);
                render_rect(fb, stride, tx - 1, by - 4, 3, SETUP_FAM_BAR_H + 8, p ? cb : ca);
            }
        }
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "PARENTS");
        if (shop) render_text(fb, stride, vx, y, 2, C_CAPT, "NONE HERE");
        else {
            render_text(fb, stride, vx, y, 2, ca, na);
            render_text(fb, stride, vx + render_text_w(na, 2) + 16, y, 2, cb, nb);
        }
        nav(fb, stride, false, "DONE", true);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else {                                                        /* SETUP_PG_CARE */
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 22, 3, C_TEXT, "CARING FOR THEM");
        static const char *const ls[] = {
            "TAP THE SURFACE TO FEED",
            "HOLD A FINGER AND THEY VISIT",
            "TAP A FISH TO CHECK ON IT",
            "DRAG THE GLASS TO WIPE ALGAE",
            "SWIPE ACROSS GRASS TO TRIM",
            "TWO TAPS FLIP THE LIGHT",
            "THEY CHOOSE. YOU CARE.",
        };
        lines_c(fb, stride, SETUP_Y + 66, 24, C_CAPT, ls, 7);
        nav(fb, stride, false, "BEGIN", true);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    }
}
