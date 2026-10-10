/* update.c - over-the-air updates: the UPDATES page, update mode's pages and
 * the flow between them. See update.h and docs/OTA.md. */
#include "update.h"
#include "render.h"
#include "tank.h"
#include "version.h"
#include "progression.h"     /* version_port_string */
#include <stdio.h>
#include <string.h>

/* the setup's palette (setup.c), so the pages read as one family */
#define C_INK    0x031015
#define C_PANEL  0x04141a
#define C_EDGE   0x9fd8e2
#define C_INNER  0x1c2f36
#define C_DIM    0x2a3f45
#define C_KEY    0x0e2229
#define C_TEXT   0xffffff
#define C_CAPT   0x9fd8e2
#define C_GO     0x155e58
#define C_GO_E   0x38dcc7
#define C_WARN   0xf2b65b     /* amber: never the only signal (Strato is color blind) */

/* the panel the setup uses: inside the bezel curve */
#define UX   32
#define UY   16
#define UW   UPD_PANEL_W
#define UH   326
#define CX   (PAGE_W / 2)

/* ---- the case-sensitive 5x7 font: every printable ASCII character ----
 * The tank's own font (render.c) is upper case; a network's name or a
 * passphrase has to show exactly as typed. Same cell (5 x 7 dots, a 6-dot
 * advance), same scale rule. Rows are 5 bits, the high bit on the left. */
static const uint8_t ASCII5X7[95][7] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /*   */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, /* ! */
    { 0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00 }, /* " */
    { 0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a }, /* # */
    { 0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04 }, /* $ */
    { 0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13 }, /* % */
    { 0x0c, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0d }, /* & */
    { 0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 }, /* ' */
    { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }, /* ( */
    { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, /* ) */
    { 0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00 }, /* * */
    { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 }, /* + */
    { 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08 }, /* , */
    { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 }, /* - */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c }, /* . */
    { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }, /* / */
    { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, /* 2 */
    { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e }, /* 3 */
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
    { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
    { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c }, /* 9 */
    { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 }, /* : */
    { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08 }, /* ; */
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, /* < */
    { 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00 }, /* = */
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, /* > */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, /* ? */
    { 0x0e, 0x11, 0x01, 0x0d, 0x15, 0x15, 0x0e }, /* @ */
    { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
    { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
    { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
    { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e }, /* D */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
    { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, /* G */
    { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* H */
    { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f }, /* L */
    { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
    { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* O */
    { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, /* P */
    { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, /* Q */
    { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
    { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, /* S */
    { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, /* W */
    { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04 }, /* Y */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, /* Z */
    { 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e }, /* [ */
    { 0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01 }, /* \ */
    { 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e }, /* ] */
    { 0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00 }, /* ^ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f }, /* _ */
    { 0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00 }, /* ` */
    { 0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f }, /* a */
    { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1e }, /* b */
    { 0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e }, /* c */
    { 0x01, 0x01, 0x0d, 0x13, 0x11, 0x11, 0x0f }, /* d */
    { 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e }, /* e */
    { 0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08 }, /* f */
    { 0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01, 0x0e }, /* g */
    { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11 }, /* h */
    { 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e }, /* i */
    { 0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c }, /* j */
    { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12 }, /* k */
    { 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* l */
    { 0x00, 0x00, 0x1a, 0x15, 0x15, 0x11, 0x11 }, /* m */
    { 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11 }, /* n */
    { 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e }, /* o */
    { 0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10 }, /* p */
    { 0x00, 0x00, 0x0d, 0x13, 0x0f, 0x01, 0x01 }, /* q */
    { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 }, /* r */
    { 0x00, 0x00, 0x0e, 0x10, 0x0e, 0x01, 0x1e }, /* s */
    { 0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06 }, /* t */
    { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d }, /* u */
    { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* v */
    { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a }, /* w */
    { 0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11 }, /* x */
    { 0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x0e }, /* y */
    { 0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f }, /* z */
    { 0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02 }, /* { */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* | */
    { 0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08 }, /* } */
    { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00 }, /* ~ */
};
int update_text_w(const char *s, int scale) { int n = (int)strlen(s); return n ? n * 6 * scale - scale : 0; }
void update_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s) {
    for (; *s; s++, x += 6 * scale) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        render_glyph(fb, stride, x, y, scale, rgb, ASCII5X7[ch - 32]);
    }
}
static void text_c(uint16_t *fb, int stride, int y, int scale, uint32_t rgb, const char *s) {   /* the tank's font, centred */
    render_text(fb, stride, CX - render_text_w(s, scale) / 2, y, scale, rgb, s);
}
static void atext_c(uint16_t *fb, int stride, int y, int scale, uint32_t rgb, const char *s) {  /* ASCII, centred */
    update_text(fb, stride, CX - update_text_w(s, scale) / 2, y, scale, rgb, s);
}
static void page_bg(uint16_t *fb, int stride) {
    render_rect(fb, stride, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, C_INK);   /* the whole frame, not just the page (render.h) */
}
static bool in_box(float x, float y, int bx, int by, int bw, int bh, int slop) {
    return x >= bx - slop && x < bx + bw + slop && y >= by - slop && y < by + bh + slop;
}

/* ====================================================================
 * 1. THE UPDATES PAGE in the tank (no radio)
 * ==================================================================== */

void render_updates_page(uint16_t *fb, int stride) {
    page_bg(fb, stride);
    text_c(fb, stride, 14, 3, C_TEXT, "UPDATES");
    char line[64];
    snprintf(line, sizeof line, "VERSION %s %s", PT_RELEASE, PT_RELEASE_STAGE);
    render_text(fb, stride, 32, 62, 2, C_TEXT, line);
    snprintf(line, sizeof line, "BUILD %s", version_port_string());
    render_text(fb, stride, 32, 84, 2, C_DIM, line);
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    if (net_port_creds_get(ssid, pass)) {
        render_text(fb, stride, 32, 122, 2, C_CAPT, "NETWORK");
        update_text(fb, stride, 32 + render_text_w("NETWORK", 2) + 14, 122, 2, C_TEXT, ssid);
    } else render_text(fb, stride, 32, 122, 2, C_CAPT, "NO NETWORK SAVED YET");
    render_text(fb, stride, 32, 152, 2, C_DIM, "THE TANK PAUSES FOR A MOMENT");
    render_text(fb, stride, 32, 174, 2, C_DIM, "AND COMES BACK BY ITSELF");
    render_button(fb, stride, UPD_CHECK_X, UPD_CHECK_Y, UPD_CHECK_W, UPD_CHECK_H, C_GO, C_GO_E, "CHECK FOR UPDATES", 2);
    if (net_port_creds_get(ssid, pass))
        render_button(fb, stride, UPD_FORGET_X, UPD_FORGET_Y, UPD_FORGET_W, UPD_FORGET_H, C_INNER, C_DIM, "FORGET NETWORK", 2);
    render_button(fb, stride, UPD_CLOSE_X, UPD_CLOSE_Y, UPD_CLOSE_W, UPD_CLOSE_H, C_INNER, C_EDGE, "CLOSE", 2);
}
int updates_page_tap(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                           /* the page's own coordinates (render.h) */
    if (x >= UPD_CLOSE_X - 8 && y >= UPD_CLOSE_Y - 4) return UPD_TAP_CLOSE;
    if (in_box(x, y, UPD_CHECK_X, UPD_CHECK_Y, UPD_CHECK_W, UPD_CHECK_H, 8)) return UPD_TAP_CHECK;
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    if (in_box(x, y, UPD_FORGET_X, UPD_FORGET_Y, UPD_FORGET_W, UPD_FORGET_H, 8) && net_port_creds_get(ssid, pass)) return UPD_TAP_FORGET;
    return UPD_TAP_NONE;
}
int updates_page_touch(float x, float y, bool down) {
    static bool s_down; static float s_px, s_py, s_lx, s_ly; static int s_hit;
    int r = UPD_TAP_NONE;
    if (down) { s_lx = x; s_ly = y; }
    if (down && !s_down) { s_px = x; s_py = y; s_hit = updates_page_tap(x, y); }
    else if (!down && s_down) {
        x = s_lx; y = s_ly;
        int h = updates_page_tap(x, y);
        float dx = x - s_px, dy = y - s_py;
        if (h && h == s_hit && dx * dx + dy * dy < 24 * 24) {
            r = h;
            if (r == UPD_TAP_FORGET) net_port_creds_forget();
        }
    }
    s_down = down;
    return r;
}

/* ====================================================================
 * 2. UPDATE MODE
 * ==================================================================== */
enum { STEP_NONE = 0, STEP_POWER, STEP_SCANNING, STEP_LIST, STEP_PASSWORD, STEP_CONNECTING, STEP_CHECKING,
       STEP_OFFER, STEP_UP_TO_DATE, STEP_INSTALLING, STEP_MESSAGE, STEP_RESTART };
/* what the message page's buttons do */
enum { ACT_NONE = 0, ACT_RETRY_CONNECT, ACT_RETYPE, ACT_SCAN, ACT_RETRY_CHECK, ACT_RETRY_INSTALL };
/* hit ids */
enum { HIT_NONE = 0, HIT_BACK, HIT_CANCEL, HIT_RESCAN, HIT_YES, HIT_NO, HIT_PRIMARY, HIT_SECONDARY, HIT_TERTIARY,
       HIT_PG_UP, HIT_PG_DOWN, HIT_ROW0 = 200, HIT_KEY0 = 100 };

static struct {
    int  step, outcome, page;
    int  battery_pct; bool on_power;
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    bool saved;                              /* the network came from the store */
    bool secured;
    net_ap_t aps[NET_SCAN_MAX]; int n_aps, list_page;
    update_manifest_t m; bool have_m;
    int  percent;
    float t;                                 /* seconds on the current page */
    /* the message page */
    char title[24], line1[40], line2[40]; bool line1_ascii;
    const char *primary, *secondary, *tertiary; int act_primary, act_secondary, act_tertiary;
    /* the keyboard */
    int  kb_mode, kb_page; bool kb_caps;
    /* the finger: the press, and the LAST position while down - a release
       reports no position on the device, the tank's touch port does the same */
    bool down; float px, py, lx, ly; int hit;
} s;

/* ---- the keyboard ----
 * 7 x 3 big keys over the whole glass (no fish here). Rows 0-1: fourteen
 * characters of the current page; row 2: the function keys. Letters on two
 * pages with a case toggle; digits and all 32 printable symbols on three. */
#define KB_CHARS 14
#define FIELD_Y 50
#define FIELD_H 34
#define FIELD_VISIBLE 30                    /* characters that fit the field at scale 2 */
/* the function row, left to right: the keys a long passphrase needs most
 * (MORE, DEL, SPACE) at the left, away from the two that leave the page
 * (BACK, JOIN) at the right - Strato, 2026-09-30: MORE beside BACK was
 * "difficult to tap without hitting back", and BACK wiped the text */
enum { FK_MORE = 0, FK_DEL, FK_SPACE, FK_SHIFT, FK_MODE, FK_BACK, FK_JOIN };
static const char *const KB_ABC[2] = { "abcdefghijklm@", "nopqrstuvwxyz_" };
static const char *const KB_123[3] = { "0123456789.-_@", "!?#$%&*+=/:;,'", "\"()[]{}<>\\|^`~" };
static int  kb_pages(void) { return s.kb_mode ? 3 : 2; }
static char kb_char(int cell) {
    const char *pg = s.kb_mode ? KB_123[s.kb_page] : KB_ABC[s.kb_page];
    char ch = pg[cell];
    if (!s.kb_mode && s.kb_caps && ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    return ch;
}
static int kb_hit(float x, float y) {                    /* HIT_KEY0 + cell, the nearest key inside the block */
    if (x < UPD_KB_X - 14 || x >= UPD_KB_X + (UPD_KB_COLS - 1) * UPD_KB_PX + UPD_KB_W + 14 || y < UPD_KB_Y - 14 || y >= UPD_KB_Y + (UPD_KB_ROWS - 1) * UPD_KB_PY + UPD_KB_H + 14) return HIT_NONE;
    int col = (int)((x - UPD_KB_X + (UPD_KB_PX - UPD_KB_W) / 2) / UPD_KB_PX), row = (int)((y - UPD_KB_Y + (UPD_KB_PY - UPD_KB_H) / 2) / UPD_KB_PY);
    if (col < 0) col = 0;
    if (col >= UPD_KB_COLS) col = UPD_KB_COLS - 1;
    if (row < 0) row = 0;
    if (row >= UPD_KB_ROWS) row = UPD_KB_ROWS - 1;
    return HIT_KEY0 + row * UPD_KB_COLS + col;
}
static void kb_key(int cell) {
    size_t n = strlen(s.pass);
    if (cell < KB_CHARS) { if (n < NET_PASS_MAX) { s.pass[n] = kb_char(cell); s.pass[n + 1] = 0; } return; }
    switch (cell - KB_CHARS) {
    case FK_MODE:  s.kb_mode ^= 1; s.kb_page = 0; break;
    case FK_SHIFT: if (!s.kb_mode) s.kb_caps = !s.kb_caps; break;
    case FK_SPACE: if (n < NET_PASS_MAX) { s.pass[n] = ' '; s.pass[n + 1] = 0; } break;
    case FK_DEL:   if (n) s.pass[n - 1] = 0; break;
    case FK_MORE:  s.kb_page = (s.kb_page + 1) % kb_pages(); break;
    default: break;                                   /* BACK / JOIN: the flow's */
    }
}
static void draw_keyboard(uint16_t *fb, int stride) {
    static const char *const FK_LABEL[7] = { "MORE", "DEL", "", "CAPS", "123", "BACK", "JOIN" };
    for (int cell = 0; cell < UPD_KB_COLS * UPD_KB_ROWS; cell++) {
        int x = UPD_KB_X + (cell % UPD_KB_COLS) * UPD_KB_PX, y = UPD_KB_Y + (cell / UPD_KB_COLS) * UPD_KB_PY;
        if (cell < KB_CHARS) {
            char lbl[2] = { kb_char(cell), 0 };
            render_rect(fb, stride, x, y, UPD_KB_W, UPD_KB_H, C_KEY); render_rect_edge(fb, stride, x, y, UPD_KB_W, UPD_KB_H, C_DIM);
            update_text(fb, stride, x + (UPD_KB_W - update_text_w(lbl, 3)) / 2, y + (UPD_KB_H - 21) / 2, 3, C_TEXT, lbl);
        } else {
            int fk = cell - KB_CHARS;
            const char *lbl = fk == FK_MODE ? (s.kb_mode ? "ABC" : "123") : FK_LABEL[fk];
            bool lit = (fk == FK_SHIFT && s.kb_caps && !s.kb_mode) || fk == FK_JOIN;
            bool dim = (fk == FK_SHIFT && s.kb_mode) || fk == FK_BACK;   /* BACK: rare, and it leaves the page - quieter */
            render_button(fb, stride, x, y, UPD_KB_W, UPD_KB_H, lit ? C_GO : dim ? C_KEY : C_INNER, dim ? C_DIM : lit ? C_GO_E : C_EDGE, lbl, 2);
            if (fk == FK_SPACE) render_rect(fb, stride, x + 10, y + UPD_KB_H / 2 + 4, UPD_KB_W - 20, 3, C_TEXT);   /* the space bar's bar */
            if (fk == FK_MORE) {                      /* the page pips under MORE */
                int n = kb_pages(), px0 = x + UPD_KB_W / 2 - (n * 8 - 4) / 2;
                for (int i = 0; i < n; i++) render_rect(fb, stride, px0 + i * 8, y + UPD_KB_H - 9, 4, 4, i == s.kb_page ? C_EDGE : C_DIM);
            }
        }
    }
}
static void draw_field(uint16_t *fb, int stride, float clock) {    /* the typed passphrase, its tail if long, a cursor */
    render_rect(fb, stride, UX, FIELD_Y, UW, FIELD_H, C_PANEL); render_rect_edge(fb, stride, UX, FIELD_Y, UW, FIELD_H, C_INNER);
    const char *p = s.pass; size_t n = strlen(p);
    char shown[FIELD_VISIBLE + 2];
    if (n > FIELD_VISIBLE) { snprintf(shown, sizeof shown, "%c%s", '<', p + n - (FIELD_VISIBLE - 1)); p = shown; }
    int x = UX + 10, y = FIELD_Y + (FIELD_H - 14) / 2;
    update_text(fb, stride, x, y, 2, C_TEXT, p);
    if ((int)(clock * 2) & 1) render_rect(fb, stride, x + update_text_w(p, 2) + (*p ? 4 : 0), y, 2, 14, C_EDGE);
    char count[12]; snprintf(count, sizeof count, "%d", (int)n);
    render_text(fb, stride, UX + UW - render_text_w(count, 1) - 8, FIELD_Y - 10, 1, C_DIM, count);
}

/* ---- the pages ---- */

static void go(int step, int page) { s.step = step; s.page = page; s.t = 0; s.hit = HIT_NONE; }
/* the message page: a title, two lines, up to three actions (row A: primary
 * left, secondary right; row B: tertiary left) and BACK TO TANK (row B,
 * right when a tertiary sits beside it, else centred) */
static void message3(const char *title, const char *l1, bool l1_ascii, const char *l2,
                     const char *primary, int act_p, const char *secondary, int act_s, const char *tertiary, int act_t) {
    snprintf(s.title, sizeof s.title, "%s", title);
    snprintf(s.line1, sizeof s.line1, "%s", l1 ? l1 : ""); s.line1_ascii = l1_ascii;
    snprintf(s.line2, sizeof s.line2, "%s", l2 ? l2 : "");
    s.primary = primary; s.act_primary = act_p; s.secondary = secondary; s.act_secondary = act_s;
    s.tertiary = tertiary; s.act_tertiary = act_t;
    go(STEP_MESSAGE, UPD_PG_MESSAGE);
}
const char *update_message_title(void) { return s.title; }
/* a message's first action: beside its second, or centred on its own over a
 * centred BACK TO TANK (2026-10-03, Strato on the watch's ODD ANSWER: a lone
 * TRY AGAIN in the left slot "doesn't look right") */
static int primary_x(void) { return s.secondary ? UPD_BTN_L_X : UPD_BTN_MID_X; }
static void message(const char *title, const char *l1, bool l1_ascii, const char *l2,
                    const char *primary, int act_p, const char *secondary, int act_s) {
    message3(title, l1, l1_ascii, l2, primary, act_p, secondary, act_s, NULL, 0);
}
static void busy(const char *title) {
    snprintf(s.title, sizeof s.title, "%s", title);
    s.percent = -1;
    s.page = UPD_PG_BUSY; s.t = 0; s.hit = HIT_NONE;
}
static void start_scan(void)    { net_port_scan_start(); s.n_aps = 0; s.list_page = 0; go(STEP_SCANNING, UPD_PG_BUSY); busy("LOOKING FOR NETWORKS"); }
static void start_connect(void) { net_port_connect_start(s.ssid, s.pass); go(STEP_CONNECTING, UPD_PG_BUSY); busy("CONNECTING TO"); }
static void start_check(void)   { net_port_check_start(); s.have_m = false; go(STEP_CHECKING, UPD_PG_BUSY); busy("CHECKING FOR UPDATES"); }
static void start_install(void) { net_port_install_start(); go(STEP_INSTALLING, UPD_PG_BUSY); busy("DOWNLOADING"); s.percent = 0; }
static void leave(int outcome)  { s.outcome = outcome; s.step = STEP_NONE; }
/* the keyboard, keeping what was typed for this network (a stray BACK or a
 * refused try must not cost a long passphrase); fresh = start empty */
static void type_password(bool fresh) {
    if (fresh) s.pass[0] = 0;
    s.kb_mode = 0; s.kb_page = 0; s.kb_caps = false; go(STEP_PASSWORD, UPD_PG_PASSWORD);
}

/* touch calibration (2026-09-30): nine crosshairs over the glass, a press
 * each; the platform logs the panel's position for each known centre and
 * the mapping is fitted from the log */
#ifdef TANK_ROUND                                       /* the bowl: a 3 x 3 the circle holds (the targets are the FRAME's px, as the panel reports) */
static const int CAL_X[3] = { 110, TANK_W / 2, TANK_W - 110 }, CAL_Y[3] = { 110, TANK_H / 2, TANK_H - 110 };
#elif defined(TANK_WATCH)                               /* the watch: in from its round corners */
static const int CAL_X[3] = { 70, TANK_W / 2, TANK_W - 70 }, CAL_Y[3] = { 70, TANK_H / 2, TANK_H - 70 };
#else
static const int CAL_X[3] = { 48, TANK_W / 2, TANK_W - 48 }, CAL_Y[3] = { 40, TANK_H / 2, TANK_H - 40 };
#endif
static int s_cal = -1;
int update_calib_target(int *x, int *y) { if (s_cal < 0 || s_cal > 8) return -1; *x = CAL_X[s_cal % 3]; *y = CAL_Y[s_cal / 3]; return s_cal; }
void update_debug_calib(void) {
    if (s.step == STEP_NONE) return;
    net_port_abort(); s_cal = 0; s.hit = HIT_NONE;
    s.step = STEP_LIST; s.page = UPD_PG_CALIB;            /* a step the tick leaves alone: the crosshairs stay up */
}
void update_debug_keyboard(void) {
    if (s.step == STEP_NONE) return;
    net_port_abort(); s.step = STEP_LIST;
    snprintf(s.ssid, sizeof s.ssid, "calibration"); s.secured = true; s.saved = false;
    type_password(true);
}
void update_begin(int battery_pct, bool on_power) {
    memset(&s, 0, sizeof s);
    s.battery_pct = battery_pct; s.on_power = on_power;
    if (!on_power && battery_pct >= 0 && battery_pct < UPD_BATTERY_MIN_PCT) { go(STEP_POWER, UPD_PG_POWER); return; }
    if (net_port_creds_get(s.ssid, s.pass)) { s.saved = true; s.secured = s.pass[0] != 0; start_connect(); }
    else start_scan();
}
bool update_active(void) { return s.step != STEP_NONE; }
int  update_outcome(void) { return s.outcome; }
int  update_page(void) { return s.page; }
const char *update_typed(void) { return s.pass; }

static void fail(int err) {
    switch (err) {
    case NET_ERR_NO_SIGNAL: message("NO SIGNAL", s.ssid, true, "IS NOT IN RANGE", "TRY AGAIN", ACT_RETRY_CONNECT, "OTHER NETWORK", ACT_SCAN); break;
    case NET_ERR_PASSWORD:  message3("NOT ACCEPTED", s.ssid, true, "REFUSED THE PASSWORD. SOME ROUTERS",
                                     "TRY AGAIN", ACT_RETRY_CONNECT, "TYPE IT AGAIN", ACT_RETYPE, "OTHER NETWORK", ACT_SCAN);
                            snprintf(s.line2, sizeof s.line2, "REFUSE THE FIRST TRIES. TRY AGAIN?"); break;
    case NET_ERR_NO_NET:    message("NO ANSWER", "THE UPDATE SERVER DID NOT", false, "ANSWER. IS THE INTERNET UP?", "TRY AGAIN", ACT_RETRY_CHECK, "OTHER NETWORK", ACT_SCAN); break;
    case NET_ERR_BAD_MANIFEST: message("ODD ANSWER", "THE UPDATE SERVER SENT", false, "SOMETHING THIS TANK CANNOT READ", "TRY AGAIN", ACT_RETRY_CHECK, NULL, 0); break;
    case NET_ERR_DOWNLOAD:  message("DOWNLOAD STOPPED", "NOTHING HAS CHANGED", false, "", "TRY AGAIN", ACT_RETRY_INSTALL, NULL, 0); break;
    case NET_ERR_VERIFY:    message("NOT VERIFIED", "THE UPDATE WAS NOT SIGNED", false, "BY AQUA PETS. NOTHING CHANGED", NULL, 0, NULL, 0); break;
    case NET_ERR_SPACE:     message("TOO BIG", "THE UPDATE DOES NOT FIT", false, "THIS TANK. NOTHING CHANGED", NULL, 0, NULL, 0); break;
    case NET_ERR_ABORTED:   message("CANCELED", "NOTHING HAS CHANGED", false, "", NULL, 0, NULL, 0); break;
    case NET_ERR_RADIO:     message("NO RADIO", "THE WI-FI RADIO DID NOT", false, "START. TRY AGAIN LATER", NULL, 0, NULL, 0); break;
    case NET_ERR_BOARD:     message("WRONG BOARD", "THIS UPDATE IS FOR", false, "ANOTHER BOARD. NOTHING CHANGED", NULL, 0, NULL, 0); break;
    default:                message("SOMETHING WENT WRONG", "NOTHING HAS CHANGED", false, "", "TRY AGAIN", ACT_SCAN, NULL, 0); break;
    }
}
static void act(int a) {
    switch (a) {
    case ACT_RETRY_CONNECT: start_connect(); break;
    case ACT_RETYPE:        type_password(false); break;
    case ACT_SCAN:          start_scan(); break;
    case ACT_RETRY_CHECK:   start_check(); break;
    case ACT_RETRY_INSTALL: start_install(); break;
    default:                leave(UPD_BACK); break;
    }
}
static bool newer(const update_manifest_t *m) { return m->release_num > (uint32_t)PT_RELEASE_NUM; }

void update_tick(float dt) {
    if (s.step == STEP_NONE) return;
    s.t += dt;
    int st;
    switch (s.step) {
    case STEP_SCANNING:
        st = net_port_scan_state();
        if (st == NET_DONE) {
            s.n_aps = net_port_scan_results(s.aps, NET_SCAN_MAX); s.list_page = 0;
            if (s.n_aps) go(STEP_LIST, UPD_PG_SCAN);
            else message("NO NETWORKS", "NOTHING IN RANGE.", false, "IS THE ROUTER ON?", "LOOK AGAIN", ACT_SCAN, NULL, 0);
        } else if (st == NET_FAILED) fail(net_port_fail_reason());
        break;
    case STEP_CONNECTING:
        st = net_port_connect_state();
        if (st == NET_DONE) {
            if (!s.saved) { net_port_creds_set(s.ssid, s.pass); s.saved = true; }
            start_check();
        } else if (st == NET_FAILED) fail(net_port_fail_reason());   /* the store is kept: routers refuse first tries, TYPE IT AGAIN replaces it */
        break;
    case STEP_CHECKING: {
        const update_manifest_t *m = NULL;
        st = net_port_check_state(&m);
        if (st == NET_DONE && m) {
            s.m = *m; s.have_m = true;
            if (strcmp(m->board, PT_BOARD) != 0)            /* another board's manifest (2026-10-02): never offered */
                message("WRONG BOARD", "THIS UPDATE IS FOR", false, "ANOTHER BOARD. NOTHING CHANGED", NULL, 0, NULL, 0);
            else if (!newer(m)) { message("UP TO DATE", "YOUR TANK HAS THE LATEST", false, "VERSION", NULL, 0, NULL, 0); s.step = STEP_UP_TO_DATE; }
            else if (m->needs_cable || (m->min_from && m->min_from > (uint32_t)PT_RELEASE_NUM))
                message("NEEDS THE CABLE", "THIS UPDATE REQUIRES", false, "THE CABLE TO INSTALL", NULL, 0, NULL, 0);   /* (2026-10-10: it named the old site before) */
            else go(STEP_OFFER, UPD_PG_OFFER);
        } else if (st == NET_FAILED) fail(net_port_fail_reason());
        break; }
    case STEP_UP_TO_DATE:
        if (s.t > UPD_UP_TO_DATE_S) leave(UPD_BACK);
        break;
    case STEP_INSTALLING: {
        int pct = s.percent;
        st = net_port_install_state(&pct);
        s.percent = pct;
        if (st == NET_DONE) go(STEP_RESTART, UPD_PG_BUSY), busy("INSTALLED"), s.percent = 100;
        else if (st == NET_FAILED) fail(net_port_fail_reason());
        break; }
    case STEP_RESTART:
        if (s.t > 1.2f) leave(UPD_RESTART);
        break;
    default: break;
    }
}

/* ---- hits ---- */
int update_hit(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                           /* the page's own coordinates (render.h) */
    switch (s.page) {
    case UPD_PG_POWER:
        return in_box(x, y, UPD_BTN_MID_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 10) ? HIT_BACK : HIT_NONE;
    case UPD_PG_BUSY:
        if (s.step == STEP_RESTART) return HIT_NONE;
        return in_box(x, y, UPD_BTN_MID_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 10) ? HIT_CANCEL : HIT_NONE;
    case UPD_PG_SCAN: {
        if (in_box(x, y, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 8)) return HIT_BACK;
        if (in_box(x, y, UPD_BTN_R_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 8)) return HIT_RESCAN;
        int pages = (s.n_aps + UPD_ROWS_PER - 1) / UPD_ROWS_PER;
        if (pages > 1 && x >= UPD_ARROW_X - 20) return y < UPD_ROW_Y0 + UPD_ROWS_PER * UPD_ROW_H / 2 ? HIT_PG_UP : HIT_PG_DOWN;
        if (y >= UPD_ROW_Y0 - 6 && y < UPD_ROW_Y0 + UPD_ROWS_PER * UPD_ROW_H) {
            int r = (int)((y - UPD_ROW_Y0 + 6) / UPD_ROW_H); if (r < 0) r = 0; if (r >= UPD_ROWS_PER) r = UPD_ROWS_PER - 1;
            int i = s.list_page * UPD_ROWS_PER + r;
            return i < s.n_aps ? HIT_ROW0 + i : HIT_NONE;
        }
        return HIT_NONE; }
    case UPD_PG_PASSWORD:
        return kb_hit(x, y);
    case UPD_PG_OFFER:
        if (in_box(x, y, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 10)) return HIT_YES;
        if (in_box(x, y, UPD_BTN_R_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 10)) return HIT_NO;
        return HIT_NONE;
    case UPD_PG_MESSAGE: {
        int by = s.primary ? UPD_BTN_Y - 50 : UPD_BTN_Y;
        if (s.primary && in_box(x, y, primary_x(), by, UPD_BTN_W, UPD_BTN_H, 8)) return HIT_PRIMARY;
        if (s.secondary && in_box(x, y, UPD_BTN_R_X, by, UPD_BTN_W, UPD_BTN_H, 8)) return HIT_SECONDARY;
        if (s.tertiary && in_box(x, y, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 8)) return HIT_TERTIARY;
        if (in_box(x, y, s.tertiary ? UPD_BTN_R_X : UPD_BTN_MID_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, 8)) return HIT_BACK;
        return HIT_NONE; }
    default: return HIT_NONE;
    }
}
const char *update_hit_name(int id) {
    static char buf[24];
    switch (id) {
    case HIT_NONE: return "none"; case HIT_BACK: return "BACK TO TANK"; case HIT_CANCEL: return "CANCEL";
    case HIT_RESCAN: return "SCAN AGAIN"; case HIT_YES: return "UPDATE"; case HIT_NO: return "NOT NOW";
    case HIT_PRIMARY: return s.primary ? s.primary : "primary"; case HIT_SECONDARY: return s.secondary ? s.secondary : "secondary";
    case HIT_TERTIARY: return s.tertiary ? s.tertiary : "tertiary";
    case HIT_PG_UP: return "page up"; case HIT_PG_DOWN: return "page down";
    default:
        if (id >= HIT_ROW0) { snprintf(buf, sizeof buf, "network %d", id - HIT_ROW0); return buf; }
        if (id >= HIT_KEY0) { snprintf(buf, sizeof buf, "key %d", id - HIT_KEY0); return buf; }
        return "?";
    }
}
static void activate(int id) {
    if (id == HIT_NONE) return;
    switch (s.page) {
    case UPD_PG_POWER: leave(UPD_BACK); break;
    case UPD_PG_BUSY:
        if (id != HIT_CANCEL) break;
        net_port_abort();
        if (s.step == STEP_INSTALLING) fail(NET_ERR_ABORTED); else leave(UPD_BACK);
        break;
    case UPD_PG_SCAN: {
        int pages = (s.n_aps + UPD_ROWS_PER - 1) / UPD_ROWS_PER;
        if (id == HIT_BACK) leave(UPD_BACK);
        else if (id == HIT_RESCAN) start_scan();
        else if (id == HIT_PG_UP)   { if (s.list_page > 0) s.list_page--; }
        else if (id == HIT_PG_DOWN) { if (s.list_page < pages - 1) s.list_page++; }
        else if (id >= HIT_ROW0 && id - HIT_ROW0 < s.n_aps) {
            const net_ap_t *ap = &s.aps[id - HIT_ROW0];
            bool same = !strcmp(s.ssid, ap->ssid);              /* back to the network already typed for: the text stays */
            snprintf(s.ssid, sizeof s.ssid, "%s", ap->ssid); s.secured = ap->secured; s.saved = false;
            if (ap->secured) type_password(!same); else { s.pass[0] = 0; start_connect(); }
        }
        break; }
    case UPD_PG_PASSWORD: {
        int cell = id - HIT_KEY0;
        if (cell == KB_CHARS + FK_BACK) { if (s.n_aps) go(STEP_LIST, UPD_PG_SCAN); else start_scan(); }   /* the text is kept; no list yet (a saved network's retype): scan */
        else if (cell == KB_CHARS + FK_JOIN) { if (strlen(s.pass) >= 8 || !s.secured) start_connect(); }
        else kb_key(cell);
        break; }
    case UPD_PG_OFFER:
        if (id == HIT_YES) start_install(); else if (id == HIT_NO) leave(UPD_BACK);
        break;
    case UPD_PG_MESSAGE:
        if (id == HIT_PRIMARY) act(s.act_primary);
        else if (id == HIT_SECONDARY) act(s.act_secondary);
        else if (id == HIT_TERTIARY) act(s.act_tertiary);
        else if (id == HIT_BACK) leave(UPD_BACK);
        break;
    default: break;
    }
}
void update_touch(float x, float y, bool down) {
    if (s.step == STEP_NONE) return;
    if (s.page == UPD_PG_CALIB) {                        /* a press advances the crosshair; the log has the position */
        if (down && !s.down) { s_cal++; if (s_cal > 8) { s_cal = -1; update_debug_keyboard(); } }
        s.down = down; return;
    }
    if (down) { s.lx = x; s.ly = y; }
    if (down && !s.down) { s.px = x; s.py = y; s.hit = update_hit(x, y); }
    else if (!down && s.down) {
        x = s.lx; y = s.ly;                              /* where the finger was, not where "nothing" is */
        int h = update_hit(x, y);
        float dx = x - s.px, dy = y - s.py;
        if (h && h == s.hit && dx * dx + dy * dy < 30 * 30) activate(h);
        s.hit = HIT_NONE;
    }
    s.down = down;
}

/* ---- drawing ---- */
static void signal_bars(uint16_t *fb, int stride, int x, int y, int rssi) {   /* 4 bars, by strength; count, not hue */
    int lit = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 4;
        render_rect(fb, stride, x + i * 7, y + 16 - h, 5, h, i < lit ? C_EDGE : C_DIM);
    }
}
static void spinner(uint16_t *fb, int stride, int cx, int cy, float clock) {
    int lit = (int)(clock * 8) & 7;
    static const int DX[8] = { 0, 13, 18, 13, 0, -13, -18, -13 }, DY[8] = { -18, -13, 0, 13, 18, 13, 0, -13 };
    for (int i = 0; i < 8; i++) render_rect(fb, stride, cx + DX[i] - 3, cy + DY[i] - 3, 6, 6, i == lit ? C_EDGE : (i + 7) % 8 == lit ? 0x5a8a94 : C_DIM);
}
static void wrap2(const char *src, char *l1, char *l2, size_t cap) {   /* two lines of at most cap-1, broken at a space */
    size_t n = strlen(src), lim = cap - 1;
    if (n <= lim) { snprintf(l1, cap, "%s", src); l2[0] = 0; return; }
    size_t cut = lim;
    while (cut > 0 && src[cut] != ' ') cut--;
    if (!cut) cut = lim;
    snprintf(l1, cap, "%.*s", (int)cut, src);
    const char *rest = src + cut; while (*rest == ' ') rest++;
    snprintf(l2, cap, "%s", rest);
    if (strlen(rest) > lim) { l2[lim - 3] = '.'; l2[lim - 2] = '.'; l2[lim - 1] = '.'; l2[lim] = 0; }
}
void render_clock_sync(uint16_t *fb, int stride, float clock) {
    page_bg(fb, stride);
    text_c(fb, stride, 120, 3, C_TEXT, "WAKING UP");
    text_c(fb, stride, 158, 2, C_CAPT, "CHECKING THE TIME");
    spinner(fb, stride, CX, 224, clock);
}
void render_update(uint16_t *fb, int stride, float clock) {
    page_bg(fb, stride);
    char line[80];
    switch (s.page) {
    case UPD_PG_POWER:
        text_c(fb, stride, 96, 3, C_TEXT, "PLUG IN TO UPDATE");
        snprintf(line, sizeof line, "THE BATTERY IS AT %d%%", s.battery_pct);
        text_c(fb, stride, 150, 2, C_CAPT, line);
        text_c(fb, stride, 176, 2, C_DIM, "AN UPDATE NEEDS THE CABLE IN");
        render_button(fb, stride, UPD_BTN_MID_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, "BACK TO TANK", 2);
        break;
    case UPD_PG_BUSY:
        text_c(fb, stride, 96, 3, C_TEXT, s.title);
        if (s.step == STEP_CONNECTING) atext_c(fb, stride, 134, 2, C_CAPT, s.ssid);
        else if (s.step == STEP_INSTALLING || s.step == STEP_RESTART) {
            snprintf(line, sizeof line, "VERSION %s", s.m.release); text_c(fb, stride, 134, 2, C_CAPT, line);
        }
        if (s.percent >= 0) {                                 /* the bar */
            const int BX = 64, BW = 320, BY = 196, BH = 16;
            render_rect(fb, stride, BX, BY, BW, BH, C_INNER); render_rect_edge(fb, stride, BX, BY, BW, BH, C_DIM);
            render_rect(fb, stride, BX + 2, BY + 2, (BW - 4) * s.percent / 100, BH - 4, C_EDGE);
            snprintf(line, sizeof line, "%d%%", s.percent); text_c(fb, stride, BY + 26, 2, C_TEXT, line);
            if (s.step == STEP_RESTART) text_c(fb, stride, 252, 2, C_CAPT, "RESTARTING");
            else text_c(fb, stride, 252, 2, C_DIM, "KEEP THE TANK ON");
        } else spinner(fb, stride, CX, 214, clock);
        if (s.step != STEP_RESTART) render_button(fb, stride, UPD_BTN_MID_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, "CANCEL", 2);
        break;
    case UPD_PG_SCAN: {
        text_c(fb, stride, 22, 2, C_CAPT, "CHOOSE YOUR NETWORK");
        int pages = (s.n_aps + UPD_ROWS_PER - 1) / UPD_ROWS_PER;
        for (int r = 0; r < UPD_ROWS_PER; r++) {
            int i = s.list_page * UPD_ROWS_PER + r; if (i >= s.n_aps) break;
            int y = UPD_ROW_Y0 + r * UPD_ROW_H;
            render_rect(fb, stride, UX, y, UW - (pages > 1 ? 36 : 0), UPD_ROW_H - 4, C_PANEL);
            render_rect_edge(fb, stride, UX, y, UW - (pages > 1 ? 36 : 0), UPD_ROW_H - 4, C_INNER);
            update_text(fb, stride, UX + 12, y + (UPD_ROW_H - 4 - 14) / 2, 2, C_TEXT, s.aps[i].ssid);
            signal_bars(fb, stride, UX + UW - (pages > 1 ? 36 : 0) - 44, y + 8, s.aps[i].rssi);
            if (!s.aps[i].secured) render_text(fb, stride, UX + UW - (pages > 1 ? 36 : 0) - 44 - 8 - render_text_w("OPEN", 1), y + 12, 1, C_DIM, "OPEN");
        }
        if (pages > 1) {                                       /* chevrons and pips on the right */
            int cx = UPD_ARROW_X + 4;
            for (int i = 0; i < 4; i++) { render_rect(fb, stride, cx - 3 - i * 3, UPD_ROW_Y0 + 20 + i * 3, 3, 3, s.list_page ? C_EDGE : C_DIM); render_rect(fb, stride, cx + i * 3, UPD_ROW_Y0 + 20 + i * 3, 3, 3, s.list_page ? C_EDGE : C_DIM); }
            for (int i = 0; i < 4; i++) { render_rect(fb, stride, cx - 3 - i * 3, UPD_ROW_Y0 + UPD_ROWS_PER * UPD_ROW_H - 30 - i * 3, 3, 3, s.list_page < pages - 1 ? C_EDGE : C_DIM); render_rect(fb, stride, cx + i * 3, UPD_ROW_Y0 + UPD_ROWS_PER * UPD_ROW_H - 30 - i * 3, 3, 3, s.list_page < pages - 1 ? C_EDGE : C_DIM); }
            for (int i = 0; i < pages; i++) render_rect(fb, stride, cx - 1, UPD_ROW_Y0 + UPD_ROWS_PER * UPD_ROW_H / 2 - pages * 4 + i * 8, 4, 4, i == s.list_page ? C_EDGE : C_DIM);
        }
        render_button(fb, stride, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, "BACK TO TANK", 2);
        render_button(fb, stride, UPD_BTN_R_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, "SCAN AGAIN", 2);
        break; }
    case UPD_PG_PASSWORD:
        render_text(fb, stride, UX + (PAGE_BOWL ? 30 : 0), 22, 2, C_CAPT, "PASSWORD FOR");
        update_text(fb, stride, UX + (PAGE_BOWL ? 30 : 0) + render_text_w("PASSWORD FOR", 2) + 12, 22, 2, C_TEXT, s.ssid);
        draw_field(fb, stride, clock);
        draw_keyboard(fb, stride);
        break;
    case UPD_PG_OFFER: {
        text_c(fb, stride, 52, 3, C_TEXT, "UPDATE READY");
        uint32_t bytes = s.m.app_size + (s.m.model_offered ? s.m.model_size : 0);
        snprintf(line, sizeof line, "VERSION %s %s, %.1f MB", s.m.release, PT_RELEASE_STAGE, bytes / 1048576.0);
        text_c(fb, stride, 92, 2, C_CAPT, line);
        char l1[32], l2[32]; wrap2(s.m.note, l1, l2, sizeof l1);
        text_c(fb, stride, 138, 2, C_TEXT, l1);
        if (l2[0]) text_c(fb, stride, 160, 2, C_TEXT, l2);
        int secs = (int)(bytes / 120000) + 10;
        if (secs < 90) snprintf(line, sizeof line, "IT TAKES ABOUT A MINUTE");
        else snprintf(line, sizeof line, "IT TAKES ABOUT %d MINUTES", (secs + 30) / 60);
        text_c(fb, stride, 208, 2, C_DIM, line);
        text_c(fb, stride, 230, 2, C_DIM, "THE TANK RESTARTS BY ITSELF");
        render_button(fb, stride, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_GO, C_GO_E, "UPDATE", 2);
        render_button(fb, stride, UPD_BTN_R_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, "NOT NOW", 2);
        break; }
    case UPD_PG_CALIB: {
        int tx, ty, i = update_calib_target(&tx, &ty);
        if (i < 0) break;
        tx -= PAGE_X; ty -= PAGE_Y;                      /* drawn on the page */
        render_rect(fb, stride, tx - 24, ty - 1, 49, 3, C_EDGE); render_rect(fb, stride, tx - 1, ty - 24, 3, 49, C_EDGE);
        render_rect(fb, stride, tx - 4, ty - 4, 9, 9, C_TEXT);
        snprintf(line, sizeof line, "TAP THE CROSS  %d OF 9", i + 1);
        text_c(fb, stride, ty < PAGE_H / 2 ? PAGE_H - 60 : 46, 2, C_CAPT, line);
        break; }
    case UPD_PG_MESSAGE: {
        text_c(fb, stride, 84, 3, C_TEXT, s.title);
        if (s.line1_ascii) atext_c(fb, stride, 130, 2, C_CAPT, s.line1); else text_c(fb, stride, 130, 2, C_CAPT, s.line1);
        if (s.line2[0]) text_c(fb, stride, 152, 2, C_CAPT, s.line2);
        if (s.step == STEP_UP_TO_DATE) {
            snprintf(line, sizeof line, "VERSION %s %s", PT_RELEASE, PT_RELEASE_STAGE); text_c(fb, stride, 186, 2, C_DIM, line);
            float left = 1.0f - s.t / UPD_UP_TO_DATE_S; if (left < 0) left = 0;
            render_rect(fb, stride, UX, 220, (int)(UW * left), 2, C_INNER);
        }
        int by = s.primary ? UPD_BTN_Y - 50 : UPD_BTN_Y;
        if (s.primary)   render_button(fb, stride, primary_x(), by, UPD_BTN_W, UPD_BTN_H, C_GO, C_GO_E, s.primary, 2);
        if (s.secondary) render_button(fb, stride, UPD_BTN_R_X, by, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, s.secondary, 2);
        if (s.tertiary)  render_button(fb, stride, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, s.tertiary, 2);
        render_button(fb, stride, s.tertiary ? UPD_BTN_R_X : UPD_BTN_MID_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_INNER, C_EDGE, "BACK TO TANK", 2);
        break; }
    default: break;
    }
}

const char *net_err_name(int err) {
    static const char *const N[] = { "none", "no signal", "wrong password", "no answer from the server", "bad manifest",
                                     "download stopped", "not verified", "too big", "canceled", "no radio" };
    return err >= 0 && err < (int)(sizeof N / sizeof N[0]) ? N[err] : "?";
}
