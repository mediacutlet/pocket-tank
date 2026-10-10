/* setup.c — first-run setup flow (see setup.h). */
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

static bool s_active;
static bool s_birth;           /* the birth flow (three pages about s_fish), not the first run */
static bool s_place;           /* the placement page (one page about s_item) */
static bool s_rename;          /* the rename page (one page about s_fish: the letter wheel, CANCEL / DONE) */
static char s_old_name[FISH_NAME_MAX + 1];   /* ... the name it came in with (CANCEL puts it back) */
static int  s_renamed;         /* a rename page just closed: the fish's slot + 1, until the platform takes it */
static int  s_item;            /* placement: the SD item being placed */
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
/* the legacy keyboards (setup.h, SETUP_KBD_*) */
static int  s_kbd;             /* the name page's design: the wheel unless the director says otherwise */
static bool s_typed;           /* this name page: the keeper has typed (the default is gone) */
static int  s_kb_page;         /* SETUP_KBD_PAGES: 0 = A-M, 1 = N-Z */

static int page_fish(void) { return s_birth || s_rename ? s_fish : s_place ? 0 : (s_page == SETUP_PG_NAME_A || s_page == SETUP_PG_LOOK_A) ? 0 : 1; }
static bool page_is_name(void) { return s_page == SETUP_PG_NAME_A || s_page == SETUP_PG_NAME_B || s_page == SETUP_PG_NAME_NEW || s_page == SETUP_PG_RENAME; }
static bool page_is_look(void) { return s_page == SETUP_PG_LOOK_A || s_page == SETUP_PG_LOOK_B; }
static bool page_is_last(void) { return s_page == SETUP_PG_CARE || s_page == SETUP_PG_FAMILY; }
static bool page_one_button(void) { return s_page == SETUP_PG_WELCOME || s_page == SETUP_PG_BORN; }   /* a lone NEXT at the foot */
static int  first_page(void) { return s_rename ? SETUP_PG_RENAME : s_place ? SETUP_PG_PLACE : s_birth ? SETUP_PG_BORN : SETUP_PG_WELCOME; }
static bool nav_on_top(void) { return (page_is_name() && s_kbd != SETUP_KBD_GRID) || page_is_look() || s_page == SETUP_PG_BUBBLES || s_page == SETUP_PG_PLACE; }
static int  place_y(void) { return s_item == 3 ? SETUP_PLACE_CORAL_Y : s_item == 4 ? SETUP_PLACE_CLUSTER_Y : SETUP_PLACE_Y; }   /* where the drag zone starts */
static const char *fish_name(const tank_t *t, int i) { return i >= 0 && i < t->n_fish ? t->fish[i].name : "?"; }
/* Modern wheel coordinates share the native device's safe settings rectangle. */
static void wheel_bounds(int *x, int *y, int *w, int *h) {
    render_settings_bounds(x, y, w, h); *x -= PAGE_X; *y -= PAGE_Y;
}
static void stage(tank_t *t);
static void tidy_name(tank_t *t, int fish);
/* the species' words (2026-10-05): what a creature is called on these pages
   - "FRY" for the classic fish, else its species ("SEAHORSE") - with its article */
static bool is_species(const fish_t *f) { return f->species > SP_FISH && f->species < SP_COUNT; }
static const char *kind_word(const fish_t *f) { return is_species(f) ? SPECIES[f->species].name : "FRY"; }
static const char *article(const char *w) { return strchr("AEIOU", w[0]) ? "AN" : "A"; }
/* a surprise: hatched as a species its parents are not (SP_MUTATE_P) */
static bool is_mutant(const tank_t *t, const fish_t *f) {
    return f->parent_a >= 0 && f->parent_a < t->n_fish && t->fish[f->parent_a].species != f->species;
}
static void name_caption(char *out, size_t n, const tank_t *t) {
    const fish_t *f = &t->fish[page_fish()];
    if (s_rename) snprintf(out, n, is_species(f) ? "RENAME YOUR %s" : "RENAME YOUR FISH", kind_word(f));
    else if (s_birth) snprintf(out, n, "NAME THE NEW %s", kind_word(f));
    else snprintf(out, n, page_fish() == 0 ? "NAME THE FIRST FISH" : "NAME THE SECOND FISH");
}

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
    s_sell_armed = false;
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
        t->stage_fish = (int8_t)page_fish(); t->stage_x = SETUP_STAGE_X;
        t->stage_y = page_is_name() ? SETUP_STAGE_NAME_Y : SETUP_STAGE_LOOK_Y;
        if (t->theme && page_is_name() && !s_kbd) {
            int x,y,w,h; wheel_bounds(&x,&y,&w,&h);
            t->stage_y = PAGE_Y + y + 65;
        }
    } else t->stage_fish = -1;
}
void setup_cancel(tank_t *t) {
    if (s_active && s_rename) { rename_end(t, false); s_renamed = 0; return; }   /* the old name back, no card to return to */
    if (s_active && s_place) progression_save(t);       /* the piece stays where it was dragged */
    s_active = false; stage(t);
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
    if (theme_active() && page_is_name() && !s_kbd) {
        int bx,by,bw,bh; wheel_bounds(&bx,&by,&bw,&bh);
        int sy=by+137, foot=by+bh-44;
        if (in_box(x,y,bx,foot,112,44,0)) return SETUP_HIT_BACK;
        if (in_box(x,y,bx+bw-112,foot,112,44,0)) return SETUP_HIT_NEXT;
        if (x < SETUP_SLOT_X-7 || x > SETUP_SLOT_X+6*SETUP_SLOT_PX+SETUP_SLOT_W+7) return 0;
        if (y>=sy-10 && y<sy+SETUP_SLOT_H+16) return SETUP_HIT_SLOT0+nearest_slot(x);
        float cx=SETUP_SLOT_X+s_slot*SETUP_SLOT_PX+SETUP_SLOT_W*0.5f;
        if (fabsf(x-cx)>22) return 0;
        if (y>=sy-54 && y<sy-10) return SETUP_HIT_UP;
        if (y>=sy+SETUP_SLOT_H+16 && y<sy+SETUP_SLOT_H+60) return SETUP_HIT_DOWN;
        return 0;
    }
    const int m = 10;                                   /* a fingertip's slop, as on the reset prompt */
    if (page_one_button())
        return in_box(x, y, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m) ? SETUP_HIT_NEXT : 0;
    if (nav_on_top()) {
        if (in_box(x, y, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (s_place && in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_SELL;
        if (!s_place && in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
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
        if (!in_box(x, y, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, 8)) return 0;
        int i = (int)((x - bx) / SETUP_DEPTH_SEG_W);
        if (i < 0) i = 0;
        if (i >= n) i = n - 1;
        return SETUP_HIT_Z0 + tank_decor_z_at(s_item, i);
    }
    if (page_is_look()) {
        /* the swatch row, a tall band (40 px above, 40 below - fingers land
           low); the nearest swatch along x, no dead space */
        int rw = (SETUP_SW_N - 1) * SETUP_SW_PX + SETUP_SW_W;
        if (!in_box(x, y, SETUP_SW_X, SETUP_SW_Y, rw, SETUP_SW_H, 40) || y >= SETUP_ACC_Y) return 0;
        int i = (int)((x - SETUP_SW_X + (SETUP_SW_PX - SETUP_SW_W) * 0.5f) / SETUP_SW_PX);
        if (i < 0) i = 0;
        if (i >= SETUP_SW_N) i = SETUP_SW_N - 1;
        return SETUP_HIT_BODY0 + i;
    }
    return 0;
}

void setup_activate(tank_t *t, int id) {
    if (!s_active || !id) return;
    fish_t *f = &t->fish[page_fish()];
    if (s_place) {                                      /* DONE saves the spot; a layer button sets the depth; SELL twice sells */
        if (id == SETUP_HIT_SELL) {
            if (!s_sell_armed) { s_sell_armed = true; return; }
            s_sell_armed = false;
            if (progression_sell(t, s_item)) { s_active = false; tank_emit(TEV_CONFIRM, -1); }
            return;
        }
        s_sell_armed = false;                           /* any other tap stands SELL down */
        if (id == SETUP_HIT_NEXT) { s_active = false; tank_emit(TEV_CONFIRM, -1); progression_save(t); }
        else if (id >= SETUP_HIT_Z0 && id < SETUP_HIT_Z0 + DECOR_Z_N)
            tank_decor_set(t, s_item, tank_decor_x(t, s_item), id - SETUP_HIT_Z0);
        else if (s_item == 3 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CORAL_N)
            tank_coral_set_rgb(t, CORAL_PAL[id - SETUP_HIT_COLOR0]);   /* the coral wears it at once; DONE saves it */
        else if (s_item == 4 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CLUSTER_SCHEME_N)
            tank_cluster_set_scheme(t, id - SETUP_HIT_COLOR0);
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
        s_page++; s_slot = 0; s_typed = false; s_kb_page = 0; stage(t);
        return;
    }
    if (id == SETUP_HIT_BACK) {
        if (page_is_name()) tidy_name(t, page_fish());
        if (s_page > first_page()) s_page--;
        s_slot = 0; s_typed = false; s_kb_page = 0; stage(t);
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
    if (page_is_look() && id >= SETUP_HIT_BODY0 && id < SETUP_HIT_BODY0 + LOOK_N) {
        fish_t *lf = &t->fish[page_fish()];
        if (is_species(lf)) {                           /* a species' creature: one of its four designs, whole */
            int v = id - SETUP_HIT_BODY0;
            if (v < SP_VARIANTS) { lf->variant = (uint8_t)v;
                                   tank_set_look(t, page_fish(), SPECIES[lf->species].var[v].color, SPECIES[lf->species].var[v].accent); }
        } else tank_set_look(t, page_fish(), LOOK_BODY[id - SETUP_HIT_BODY0], 0);   /* the accent stays its secret */
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
            tank_decor_set(t, s_item, x, tank_decor_z(t, s_item)); s_spun = true;   /* the piece comes to the finger */
        }
    } else if (down && s_page == SETUP_PG_BUBBLES && s_spun) {
        tank_set_bubble_x(t, x);                            /* ... and follows it */
    } else if (down && s_page == SETUP_PG_PLACE && s_spun) {
        tank_decor_set(t, s_item, x, tank_decor_z(t, s_item));
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
/* a little shipwreck for its depth tiles (2026-10-10): a tilted hull with two holes, a mast stump */
static void wreck_glyph(uint16_t *fb, int stride, int cx, int y_bot) {
    const uint32_t wood = 0x6b4e35, dark = 0x3a2a1c, hole = 0x0b1a22;
    for (int i = 0; i < 12; i++) render_rect(fb, stride, cx - 20 + i * 3 + (i < 2 ? 2 - i : 0), y_bot - 14 + (i > 9 ? i - 9 : 0), 3, 12 - (i < 2 ? 2 - i : 0) - (i > 9 ? i - 9 : 0), wood);
    render_rect(fb, stride, cx - 18, y_bot - 15, 36, 1, dark); render_rect(fb, stride, cx - 16, y_bot - 9, 32, 1, dark);
    render_rect(fb, stride, cx - 11, y_bot - 11, 5, 5, hole); render_rect(fb, stride, cx + 5, y_bot - 12, 6, 6, hole);
    render_rect(fb, stride, cx - 1, y_bot - 28, 2, 14, dark); render_rect(fb, stride, cx - 5, y_bot - 24, 10, 1, dark);
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
/* a depth tile: the plant's - two leaves and the keeper's first fish, in
 * the order the choice means - BEHIND (the plant behind the fish): leaves,
 * then the fish over them; AMONG: one leaf, the fish, the other leaf; IN
 * FRONT: the fish, then both leaves over it. The castle's (2026-09-16):
 * the castle and two leaves - BEHIND: the castle, the leaves over it; IN
 * FRONT: the leaves, the castle over them (its depths mean the plant layer) */
static void depth_tile(const tank_t *t, uint16_t *fb, int stride, int x, int y, int w, int h, int z, int item, float clock) {
    const int cx = x + w / 2, cy = y + h / 2, lh = h - 6, yb = y + h - 3;
    const uint32_t la = 0x8dbb48, lb = 0x6c9d38;
    const fish_t *who = t->n_fish > 0 ? &t->fish[0] : NULL;
    if (item == SD_ITEM_WRECK_IDX) {                    /* the wreck: as the castle's, behind or in front of the leaves */
        if (z == DECOR_Z_BACK) wreck_glyph(fb, stride, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) wreck_glyph(fb, stride, cx, yb);
        return;
    }
    if (item == 2) {
        if (z == DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        return;
    }
    if (item == 4) {                                    /* the cluster: the same three readings as the coral's */
        const cluster_scheme_t *sc = &CLUSTER_SCHEMES[tank_cluster_scheme(t)];
        const int kx = cx - 18;
        if (z == DECOR_Z_BACK) cluster_glyph(fb, stride, kx, yb, sc);
        leaf_glyph(fb, stride, cx - 44, yb, lh, la);
        if (z == DECOR_Z_MIDDLE) cluster_glyph(fb, stride, kx, yb, sc);
        leaf_glyph(fb, stride, cx + 6, yb, lh, lb);
        if (who) render_fish_portrait(fb, stride, (float)(cx + 26), (float)cy, 0.62f, who, clock);
        if (z == DECOR_Z_FRONT) cluster_glyph(fb, stride, kx, yb, sc);
        return;
    }
    if (item == 3) {                                    /* the coral, left of centre, the fish passing to its right: behind the
                                                           grass and the fish / among the grass, the fish in front / over all */
        uint32_t rgb = tank_coral_rgb(t);
        const int kx = cx - 20;
        if (z == DECOR_Z_BACK) coral_glyph(fb, stride, kx, yb, rgb);
        leaf_glyph(fb, stride, cx - 36, yb, lh, la);
        if (z == DECOR_Z_MIDDLE) coral_glyph(fb, stride, kx, yb, rgb);
        leaf_glyph(fb, stride, cx - 6, yb, lh, lb);
        if (who) render_fish_portrait(fb, stride, (float)(cx + 14), (float)cy, 0.62f, who, clock);
        if (z == DECOR_Z_FRONT) coral_glyph(fb, stride, kx, yb, rgb);
        return;
    }
    if (z == DECOR_Z_BACK)  { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
    if (z == DECOR_Z_MIDDLE)  leaf_glyph(fb, stride, cx - 8, yb, lh, la);
    if (who) render_fish_portrait(fb, stride, (float)cx, (float)cy, 0.62f, who, clock);
    if (z == DECOR_Z_MIDDLE)  leaf_glyph(fb, stride, cx + 8, yb, lh, lb);
    if (z == DECOR_Z_FRONT) { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
}
static void panel(uint16_t *fb, int stride) {
    render_rect(fb, stride, SETUP_X, SETUP_Y, SETUP_W, SETUP_H, C_PANEL);
    render_rect_edge(fb, stride, SETUP_X, SETUP_Y, SETUP_W, SETUP_H, C_EDGE);
    render_rect_edge(fb, stride, SETUP_X + 1, SETUP_Y + 1, SETUP_W - 2, SETUP_H - 2, C_INNER);
}

void render_setup(const tank_t *t, uint16_t *fb, int stride, float clock) {
    render_use_theme(t->theme);
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
        render_button(fb, stride, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "DONE", 2);
        /* SELL, top left (2026-09-24): a tap arms it and names the refund, a
           second tap sells the piece back to the shop and closes the page */
        if (s_sell_armed) { char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_sell_value(s_item));
                            render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, ok, 2); }
        else render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_INNER, C_DIM, "SELL", 2);
        /* the DEPTH bar: one outlined box, three joined segments, the chosen
           one lit; a picture tile on each and the word under it */
        static const char *const zl[DECOR_Z_N] = { "BEHIND", "AMONG", "IN FRONT" };
        static const char *const hint[DECOR_Z_N] = { "THE FISH SWIM IN FRONT OF IT", "THE FISH SWIM THROUGH IT", "THE FISH SWIM BEHIND IT" };
        static const char *const castle_hint[DECOR_Z_N] = { "THE PLANTS GROW IN FRONT OF IT", "", "IT STANDS IN FRONT OF THE PLANTS" };
        static const char *const coral_hint[DECOR_Z_N] = { "BEHIND THE GRASS AND THE FISH", "", "IN FRONT OF EVERYTHING" };
        int z = tank_decor_z(t, s_item), zi = tank_decor_z_index(s_item, z);
        int n = tank_decor_z_count(s_item), bw = n * SETUP_DEPTH_SEG_W, bx = (PAGE_W - bw) / 2;   /* the item's own depths */
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
        if (s_item == 4) {
            /* the cluster: a LOOK row of three tiles - the coral, the tubes and
               the brain as three bands over the look's name - the pick ringed */
            int look = tank_cluster_scheme(t);
            render_rect_blend(fb, stride, -PAGE_X, SETUP_LOOK_Y - 22, TANK_W, SETUP_LOOK_H + 30, C_PANEL, 110);
            render_text(fb, stride, SETUP_LOOK_X + 2, SETUP_LOOK_Y - 18, 2, C_CAPT, "LOOK");
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
            (void)coral_hint;
            for (int i = 0; i < SETUP_COL_N; i++) {
                int x = SETUP_COL_X + i * SETUP_COL_PX;
                bool on = CORAL_PAL[i] == rgb;
                render_rect(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, CORAL_PAL[i]);
                render_rect_edge(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, SETUP_COL_Y + 1, SETUP_COL_W - 2, SETUP_COL_H - 2, C_TEXT);
            }
        } else text_c(fb, stride, CX, SETUP_DEPTH_HINT_Y, 2, C_CAPT, s_item == 2 || s_item == SD_ITEM_WRECK_IDX ? castle_hint[z] : hint[z]);
        float x0 = tank_decor_x(t, s_item) - tank_decor_half_w(s_item) - 10, x1 = tank_decor_x(t, s_item) + tank_decor_half_w(s_item) + 10, top = TANK_BOT - 16 - 40;
        if (s_item == 0) tank_veg_bed(t, 3, NULL, NULL, &top, NULL);   /* the leaves' reach */
        if (s_item == 2) top = TANK_BOT - 16 - 146;                    /* the tallest spire */
        if (s_item == 3) top = TANK_BOT - 14 + DECOR_SINK - 92;        /* the coral's top tip */
        if (s_item == 4) top = TANK_BOT - 14 + DECOR_SINK - 120;       /* the cluster's tallest tube */
        if (s_item == SD_ITEM_WRECK_IDX) top = TANK_BOT - 14 + DECOR_SINK - 84;   /* the wreck's mast */
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
        char ncap[40]; name_caption(ncap, sizeof ncap, t);
        text_c(fb, stride, CX, SETUP_Y + (grid ? 12 : 7), 2, C_CAPT, ncap);
        if (grid) render_creature_preview(fb, stride, SETUP_X + 88, SETUP_Y + 72, 2.0f * 22 / fmaxf(22, render_species_half_len(f->species)),
                                          f->species, f->variant, f->color, f->fin, f->accent, clock);   /* an eel fits the box (2026-10-05) */
        const int SL = 22, NX = grid ? SETUP_X + 160 : CX - FISH_NAME_MAX * SL / 2 + 2, NY = grid ? SETUP_Y + 58 : SETUP_TOP_BTN_Y + (SETUP_BTN_H - 28) / 2;
        int n = name_len(f);
        for (int i = 0; i < FISH_NAME_MAX; i++) {
            int x = NX + i * SL;
            bool cursor = i == n && ((int)(clock * 2) & 1);
            render_rect(fb, stride, x, NY + 26, SL - 4, 2, cursor ? C_EDGE : i < n ? f->color : C_DIM);
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
        int bx=0,by=0,bw=0,bh=0; wheel_bounds(&bx,&by,&bw,&bh);
        int sy=theme_active() ? by+137 : SETUP_SLOT_Y;
        const fish_t *f = &t->fish[page_fish()];
        fish_ring(fb, stride, f, render_fish_ring_r(f) + 6, f->color);
        render_rect_blend(fb, stride, -PAGE_X, sy - SETUP_ARROW_GAP - 2, TANK_W, SETUP_SLOT_H + 2 * SETUP_ARROW_GAP + 36, C_PANEL, 150);
        char ncap[40]; name_caption(ncap, sizeof ncap, t);
        if (theme_active()) {
            text_c(fb,stride,CX,by+4,2,C_CAPT,"NAME YOUR PET");
            render_button(fb,stride,bx,by+bh-44,112,44,C_KEY,C_DIM,s_rename?"CANCEL":"BACK",2);
            render_button(fb,stride,bx+bw-112,by+bh-44,112,44,C_GO,C_GO_E,s_rename?"DONE":"NEXT",2);
        } else {
            text_c(fb,stride,CX,SETUP_Y+7,2,C_CAPT,ncap);
            nav(fb,stride,true,s_rename?"DONE":"NEXT",s_rename);
        }
        int n = name_len(f);
        if (s_slot > n) s_slot = n;
        for (int i = 0; i < FISH_NAME_MAX; i++) {
            int x = SETUP_SLOT_X + i * SETUP_SLOT_PX;
            bool on = i == s_slot, reach = i <= n;
            uint32_t col = theme_active() ? (on ? C_TEXT : reach ? C_EDGE : C_DIM) : on ? f->color : reach ? dim(f->color, 55) : C_DIM;
            int v = slot_val(f, i);
            if (v) { char ch[2] = { (char)('A' + v - 1), 0 }; render_text(fb, stride, x, sy, SETUP_SLOT_SCALE, col, ch); }
            render_rect(fb, stride, x, sy + SETUP_SLOT_H + 8, SETUP_SLOT_W, 4,
                        on && !v && ((int)(clock * 2) & 1) ? C_TEXT : col);
            if (on) {
                chevron(fb, stride, x + SETUP_SLOT_W / 2, sy - SETUP_ARROW_GAP, true, C_EDGE);
                chevron(fb, stride, x + SETUP_SLOT_W / 2, sy + SETUP_SLOT_H + SETUP_ARROW_GAP + 4, false, C_EDGE);
            }
        }
        if (theme_active()) text_c(fb,stride,CX,by+238,2,C_CAPT,"SWIPE TO ROLL");
        else { text_c(fb,stride,CX,sy+SETUP_SLOT_H+96,2,C_CAPT,"SWIPE A LETTER UP OR DOWN"); dots(fb,stride,PAGE_H-14); }
    } else if (page_is_look()) {
        /* straight on the tank again: the ringed FRY is the preview (no
           grown-up look - that is the surprise); a row of body swatches; the
           accent a "?" that its first growth spurt answers */
        const fish_t *f = &t->fish[page_fish()];
        fish_ring(fb, stride, f, render_fish_ring_r(f) + 6, f->color);
        render_rect_blend(fb, stride, -PAGE_X, SETUP_SW_Y - 30, TANK_W, SETUP_ACC_Y + SETUP_SW_H + 16 - (SETUP_SW_Y - 30), C_PANEL, 110);
        char cap[FISH_NAME_MAX + 16]; snprintf(cap, sizeof cap, "A COLOR FOR %s", f->name);
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, cap);
        nav(fb, stride, true, "NEXT", false);
        /* (a species' creature - never one of the founding pair today, but a
           director's tank may stage one - picks among its species' four
           designs instead: the body swatches are the designs' bodies, and a
           design brings its own markings) */
        bool sp = is_species(f);
        render_text(fb, stride, SETUP_SW_X + 2, SETUP_SW_Y - 20, 2, C_CAPT, sp ? "DESIGN" : "BODY");
        for (int i = 0; i < (sp ? SP_VARIANTS : SETUP_SW_N); i++) {
            int x = SETUP_SW_X + i * SETUP_SW_PX;
            uint32_t body = sp ? SPECIES[f->species].var[i].color : LOOK_BODY[i];
            bool on = sp ? f->variant == i : LOOK_BODY[i] == f->color;
            render_rect(fb, stride, x, SETUP_SW_Y, SETUP_SW_W, SETUP_SW_H, body);
            render_rect_edge(fb, stride, x, SETUP_SW_Y, SETUP_SW_W, SETUP_SW_H, on ? C_TEXT : C_DIM);
            if (on) render_rect_edge(fb, stride, x + 1, SETUP_SW_Y + 1, SETUP_SW_W - 2, SETUP_SW_H - 2, C_TEXT);
        }
        render_text(fb, stride, SETUP_SW_X + 2, SETUP_ACC_Y - 20, 2, C_CAPT, "ACCENT");
        render_rect(fb, stride, SETUP_SW_X, SETUP_ACC_Y, SETUP_SW_W, SETUP_SW_H, C_INNER);
        render_rect_edge(fb, stride, SETUP_SW_X, SETUP_ACC_Y, SETUP_SW_W, SETUP_SW_H, C_DIM);
        render_text(fb, stride, SETUP_SW_X + (SETUP_SW_W - 20) / 2, SETUP_ACC_Y + (SETUP_SW_H - 28) / 2, 4, C_CAPT, "?");
        render_text(fb, stride, SETUP_SW_X + SETUP_SW_W + 16, SETUP_ACC_Y + 12, 2, C_CAPT, "ITS MARKINGS COME IN");
        render_text(fb, stride, SETUP_SW_X + SETUP_SW_W + 16, SETUP_ACC_Y + 32, 2, C_CAPT, "AS IT GROWS UP");
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_BORN) {
        /* the announcement, over the live tank: the fry wears a double ring
           wherever it hatched (no stage - it stays in the grass it was born
           in), its parents are named, one button leads on */
        const fish_t *f = &t->fish[page_fish()];
        float r = render_fish_ring_r(f) + 6 + 3 * sinf(clock * 3);          /* a ring that breathes */
        fish_ring(fb, stride, f, r, f->color);
        fish_ring(fb, stride, f, r + 8, C_EDGE);
        render_rect_blend(fb, stride, -PAGE_X, 92, TANK_W, 116, C_PANEL, 150);
        /* (2026-10-05) a species' newborn is named for what it is - "A NEW
           SEAHORSE!" - and a surprise (a classic pair's fry hatched as a
           species, SP_MUTATE_P) gets its own announcement */
        char head[40], l1[FISH_NAME_MAX * 2 + 24], l2[40];
        bool mutant = is_mutant(t, f);
        if (mutant) snprintf(head, sizeof head, "A SURPRISE!");
        else snprintf(head, sizeof head, "A NEW %s!", kind_word(f));
        text_c(fb, stride, CX, 104, 3, C_TEXT, head);
        snprintf(l1, sizeof l1, "BORN TO %s AND %s", fish_name(t, f->parent_a), fish_name(t, f->parent_b));
        text_c(fb, stride, CX, 140, 2, C_CAPT, l1);
        if (mutant) snprintf(l2, sizeof l2, "AS %s %s!", article(kind_word(f)), kind_word(f));
        else snprintf(l2, sizeof l2, "DOWN IN THE GRASS.");
        text_c(fb, stride, CX, 162, 2, C_CAPT, l2);
        text_c(fb, stride, CX, 184, 2, C_CAPT, "GO AND SAY HELLO.");
        render_button(fb, stride, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "MEET IT", 2);
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_FAMILY) {
        /* the family page: a panel (the fry on its own), its name, its
           portrait as it is now - a fry, no markings yet - and what it
           inherited: whose body, whose markings, and how bold and sociable
           it is next to its parents' marks */
        const fish_t *f = &t->fish[page_fish()];
        const fish_t *pa = f->parent_a >= 0 && f->parent_a < t->n_fish ? &t->fish[f->parent_a] : NULL;
        const fish_t *pb = f->parent_b >= 0 && f->parent_b < t->n_fish ? &t->fish[f->parent_b] : NULL;
        const char *na = fish_name(t, f->parent_a), *nb = fish_name(t, f->parent_b);
        uint32_t ca = pa ? pa->color : C_DIM, cb = pb ? pb->color : C_DIM;
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 20, 3, C_TEXT, f->name);
        float ps = f->size * 2.0f;                                   /* a portrait, twice life size (a fry) ... */
        if (ps > 1.2f) ps = 1.2f;                                    /* ... but a juvenile's ring must clear the name */
        float ru = render_fish_ring_r(f) / f->size;                  /* a long creature (an eel) to the same ring (2026-10-05) */
        if (ps * ru > 1.2f * 17) ps = 1.2f * 17 / ru;
        render_ring(fb, stride, CX, SETUP_FAM_PORTRAIT_Y, ru * ps + 6, f->color);
        render_fish_portrait(fb, stride, CX, SETUP_FAM_PORTRAIT_Y, ps, f, clock);
        int y = SETUP_FAM_ROW_Y, vx = SETUP_FAM_VALUE_X;
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "BODY");
        render_rect(fb, stride, vx, y - 1, 16, 16, f->color); render_rect_edge(fb, stride, vx, y - 1, 16, 16, C_INNER);
        text_named(fb, stride, vx + 24, y, "FROM ", na, ca, NULL);
        y += SETUP_FAM_ROW_DY;
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "MARKINGS");
        render_rect(fb, stride, vx, y - 1, 16, 16, f->accent); render_rect_edge(fb, stride, vx, y - 1, 16, 16, C_INNER);
        text_named(fb, stride, vx + 24, y, "FROM ", nb, cb, NULL);
        static const char *const TRAIT[2] = { "BOLD", "SOCIAL" };
        for (int k = 0; k < 2; k++) {                                /* the fry's own bar, the parents' ticks */
            y += SETUP_FAM_ROW_DY;
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
        y += SETUP_FAM_ROW_DY;
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "PARENTS");
        render_text(fb, stride, vx, y, 2, ca, na);
        render_text(fb, stride, vx + render_text_w(na, 2) + 16, y, 2, cb, nb);
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
