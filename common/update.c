/* update.c - over-the-air updates: the UPDATES page, update mode's pages and
 * the flow between them. See update.h and docs/OTA.md. */
#include "update.h"
#include "render.h"
#include "tank.h"
#include "version.h"
#include "progression.h"     /* version_port_string */
#include "touch_gate.h"
#include "wifi_nets.h"
#include <stdio.h>
#include <string.h>

/* the setup's palette (setup.c), so the pages read as one family */
#define C_INK    0x031015
#define C_PANEL  0x04141a
#define C_EDGE   0x9fd8e2
#define C_INNER  0x1c2f36
#define C_DIM    0x2a3f45
#define C_QUIET  0x7f9aa1     /* quiet TEXT (render.c MSP_QUIET, 0.3.9.1: C_DIM text was too dark to read); C_DIM stays for shapes */
#define C_KEY    0x0e2229
#define C_TEXT   0xffffff
#define C_CAPT   0x9fd8e2
#define C_GO     0x155e58
#define C_GO_E   0x38dcc7
#define C_FILL   0x2f7f8a     /* a hold filling (REMOVE's deep teal: the white words read on it and on C_GO) */
_Static_assert(UPD_CLOSE_W == MSP_CLOSE_W && UPD_CLOSE_H == SET_NAV_SQ, "the UPDATES page's BACK is the GRAPHICS page's box: render_settings_from_updates reads its footprint there");
#define UPD_HOLD_S 1.0f
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

/* FORGET NETWORK ARMS (0.4.1 audit round 1, TCH-10; Strato 11-05: "fix this, give a confirmation 'sure?'"): one tap
   forgot the saved Wi-Fi, and with it the morning clock check and the night's power-off on a board with no clock chip.
   Now the first tap arms it - the button says SURE? - and a second tap forgets, the way the keeper card's SELL arms:
   not within OPEN_WAIT_US of the tap before it, read at the second tap's press (a double tap, a mash or a firm second
   press is spent: touch_gate.h THE TOUCH RULE, no restart). Any other release on the page, or the page
   opening again, disarms it. */
/* THREE NETWORKS (0.4.2, wifi_nets.h): a row each, in the order the tank tries them, each with its own FORGET that
   arms and forgets the same way - the row armed is the one that says SURE?, and a tap on another row's FORGET arms
   that one instead (never forgets the first). The network is forgotten by the NAME that was armed. The one the tank
   was on last has a check mark before its name and LAST USED under it (a shape and words, never a color alone; the
   radio is off in the tank, so no network is ever "connected" on this page). */
/* ARMED BY NAME (0.4.2 review): the name whose FORGET says SURE?, "" = none. Its row is looked up each time, so a
   list that changes under the page (the director's wifi set / forget) moves SURE? with its network, and a network
   that has left the list is not armed any more */
static int64_t s_forget_at; static char s_forget_ssid[NET_SSID_MAX + 1];
/* THE TWO FACES (0.4.2, update.h): UPDATES, and WI-FI behind its door. s_face_at: when the face last turned by a tap -
   a press that begins within OPEN_WAIT_US of it is the turning tap's double (no tap; nothing waits, nothing restarts) */
static bool s_nets; static int64_t s_face_at; static bool s_face_on;
void updates_page_open(void) { s_forget_ssid[0] = 0; s_nets = false; s_face_on = false; }
void updates_page_show_nets(bool on) { s_nets = on; s_forget_ssid[0] = 0; s_face_on = false; }
bool updates_page_nets(void) { return s_nets; }
int  updates_forget_row(void) {
    if (!s_forget_ssid[0]) return -1;
    const int r = wifi_nets_find(net_port_nets(), s_forget_ssid);
    if (r < 0) s_forget_ssid[0] = 0;
    return r;
}
bool updates_forget_armed(void) { return updates_forget_row() >= 0; }
static void check_mark(uint16_t *fb, int stride, int x, int y, uint32_t rgb) {   /* 15 x 13, beside a size-2 line */
    static const int8_t CK[7][2] = { { 0, 6 }, { 2, 8 }, { 4, 10 }, { 6, 8 }, { 8, 6 }, { 10, 4 }, { 12, 2 } };
    for (int k = 0; k < 7; k++) render_rect(fb, stride, x + CK[k][0], y + CK[k][1], 3, 3, rgb);
}
static void wifi_mark(uint16_t *fb, int stride, int cx, int by, uint32_t rgb) {   /* the door's picture: a dot and two arcs over it, 27 x 15 */
    for (int dy = -14; dy <= 0; dy++) for (int dx = -14; dx <= 14; dx++) {
        const int r2 = dx * dx + dy * dy;
        const bool dot = r2 <= 5, fan = (dx < 0 ? -dx : dx) <= -dy + 1;
        if (dot || (fan && ((r2 >= 36 && r2 <= 72) || (r2 >= 132 && r2 <= 196)))) render_rect(fb, stride, cx + dx, by + dy, 1, 1, rgb);
    }
}
static void page_title(uint16_t *fb, int stride, const char *s) { text_c(fb, stride, SET_TITLE_Y, 3, C_TEXT, s); }
static void page_back(uint16_t *fb, int stride) { render_button(fb, stride, UPD_CLOSE_X, UPD_CLOSE_Y, UPD_CLOSE_W, UPD_CLOSE_H, C_INNER, C_EDGE, "BACK", 2); }
int updates_info_fit(const char *label, const char *value, char *out, size_t cap) {
    const int right = UPD_X + UPD_W;
    snprintf(out, cap, "%s", value);
    if (UPD_VAL_X + render_text_w(out, 2) <= right) return UPD_VAL_X;          /* in the column */
    const int min_x = UPD_X + render_text_w(label, 2) + 14;
    wifi_name_fit(out, cap, value, (right - min_x + 2) / 12);                 /* (a size-2 letter: 12 px, the last one 10) */
    return right - render_text_w(out, 2);
}
static void info_row(uint16_t *fb, int stride, int row, const char *label, const char *value) {
    char shown[40]; const int x = updates_info_fit(label, value, shown, sizeof shown);
    render_text(fb, stride, UPD_X, UPD_INFO_Y + row * UPD_INFO_DY, 2, C_CAPT, label);
    render_text(fb, stride, x, UPD_INFO_Y + row * UPD_INFO_DY, 2, C_TEXT, shown);
}
static void render_nets_face(uint16_t *fb, int stride) {
    page_title(fb, stride, "WI-FI");
    const int armed = updates_forget_row();
    const wifi_nets_t *w = net_port_nets(); const int nets = w->n;
    const int ty = (UPD_NET_H - 14) / 2;                               /* a size-2 line in the middle of a row */
    for (int i = 0; i < WIFI_NETS_MAX; i++) {
        const int y = UPD_NET_Y0 + i * UPD_NET_DY;
        if (i >= nets) {                                               /* an open place */
            render_rect_edge(fb, stride, UPD_X, y, UPD_W, UPD_NET_H, C_INNER);
            render_text(fb, stride, UPD_NAME_X, y + ty, 2, C_QUIET, "EMPTY");
            continue;
        }
        render_rect(fb, stride, UPD_X, y, UPD_W, UPD_NET_H, C_PANEL); render_rect_edge(fb, stride, UPD_X, y, UPD_W, UPD_NET_H, C_INNER);
        char shown[UPD_NAME_CHARS + 1]; wifi_name_fit(shown, sizeof shown, w->net[i].ssid, UPD_NAME_CHARS);
        update_text(fb, stride, UPD_NAME_X, y + ty, 2, C_TEXT, shown);
        if (i == 0 && w->net[0].joined) check_mark(fb, stride, UPD_X + 8, y + ty, C_TEXT);
        if (armed == i) render_button(fb, stride, UPD_FORGET_X, y, UPD_FORGET_W, UPD_FORGET_H, C_GO, C_GO_E, "SURE?", 2);
        else render_button(fb, stride, UPD_FORGET_X, y, UPD_FORGET_W, UPD_FORGET_H, C_INNER, C_DIM, "FORGET", 2);
    }
    if (nets && w->net[0].joined) {                                    /* what the mark means, in words */
        check_mark(fb, stride, UPD_X + 8, UPD_LEGEND_Y, C_QUIET);
        render_text(fb, stride, UPD_NAME_X, UPD_LEGEND_Y, 2, C_QUIET, "LAST USED");
    }
    render_button(fb, stride, UPD_X, UPD_ADD_Y, UPD_W, UPD_BTN40, C_GO, C_GO_E, "ADD A NETWORK", 2);
    page_back(fb, stride);
}
void render_updates_page(uint16_t *fb, int stride) {
    page_bg(fb, stride);
    if (s_nets) { render_nets_face(fb, stride); return; }
    page_title(fb, stride, "UPDATES");
    char line[64];
    snprintf(line, sizeof line, "%s %s", PT_RELEASE, PT_RELEASE_STAGE);
    info_row(fb, stride, 0, "VERSION", line);
    info_row(fb, stride, 1, "BUILD", version_port_string());
    /* 0.4.1: the code a keeper gives Turbo (/crew add), and once the crew
       check said yes, the words say so (never the color alone) */
    char code[NET_CODE_LEN + 1];
    if (net_port_board_code(code)) info_row(fb, stride, 2, net_port_crew() ? "HATCHERY CREW" : "BOARD CODE", code);
    render_button(fb, stride, UPD_CHECK_X, UPD_CHECK_Y, UPD_CHECK_W, UPD_CHECK_H, C_GO, C_GO_E, "CHECK FOR UPDATES", 2);
    render_text(fb, stride, UPD_X, UPD_TEXT_Y, 2, C_QUIET, "WHILE UPDATING, THE BOARD");      /* Strato's words, 2026-11-04 */
    render_text(fb, stride, UPD_X, UPD_TEXT_Y + 20, 2, C_QUIET, "PAUSES AND REBOOTS ITSELF");
    {   /* THE WI-FI DOOR: a settings tile across the page - the picture, a small name over what is saved, the arrow that says it opens */
        const int nets = net_port_creds_count(), y = UPD_DOOR_Y;
        render_button(fb, stride, UPD_X, y, UPD_W, UPD_DOOR_H, C_INNER, C_DIM, "", 2);
        wifi_mark(fb, stride, UPD_X + 25, y + 28, C_EDGE);
        if (!nets) snprintf(line, sizeof line, "NO NETWORK SAVED");
        else snprintf(line, sizeof line, "%d NETWORK%s SAVED", nets, nets == 1 ? "" : "S");
        render_text(fb, stride, UPD_X + 50, y + 6, 1, C_QUIET, "WI-FI");
        render_text(fb, stride, UPD_X + 50, y + 18, 2, C_TEXT, line);
        for (int i = 0; i < 7; i++) render_rect(fb, stride, UPD_X + UPD_W - 19 + i, y + UPD_DOOR_H / 2 - (6 - i), 1, 2 * (6 - i) + 1, C_EDGE);
    }
    page_back(fb, stride);
}
/* the taps. BACK: the settings foot's band (no reach above it, 14 under, 12 to each side - the GRAPHICS page's BACK);
   the wide buttons 8 px out; a FORGET 8 to the sides and 3 up and down (half the gap to the next row's) */
int updates_page_tap(float x, float y) {
    x -= PAGE_X; y -= PAGE_Y;                           /* the page's own coordinates (render.h) */
    if (y >= UPD_CLOSE_Y && y < UPD_CLOSE_Y + UPD_CLOSE_H + 14 && x >= UPD_CLOSE_X - 12 && x < UPD_CLOSE_X + UPD_CLOSE_W + 12)
        return s_nets ? UPD_TAP_NETS_BACK : UPD_TAP_CLOSE;
    if (!s_nets) {
        if (in_box(x, y, UPD_CHECK_X, UPD_CHECK_Y, UPD_CHECK_W, UPD_CHECK_H, 8)) return UPD_TAP_CHECK;
        if (in_box(x, y, UPD_X, UPD_DOOR_Y, UPD_W, UPD_DOOR_H, 8)) return UPD_TAP_NETS;
        return UPD_TAP_NONE;
    }
    if (in_box(x, y, UPD_X, UPD_ADD_Y, UPD_W, UPD_BTN40, 8)) return UPD_TAP_ADD;
    if (x >= UPD_FORGET_X - 8 && x < UPD_FORGET_X + UPD_FORGET_W + 8 && y >= UPD_FORGET_Y - 3 && y < UPD_FORGET_Y + WIFI_NETS_MAX * UPD_NET_DY) {
        const int r = (int)(y - (UPD_FORGET_Y - 3)) / UPD_NET_DY;                       /* a saved network's FORGET, its own row's */
        if (y < UPD_FORGET_Y + r * UPD_NET_DY + UPD_FORGET_H + 3 && r < net_port_creds_count()) return UPD_TAP_FORGET + r;
    }
    return UPD_TAP_NONE;
}
static bool s_down; static float s_px, s_py, s_lx, s_ly; static int s_hit; static int64_t s_press_us;   /* updates_page_touch's press in hand */
/* A PRESS CUT (0.4.1 audit round 2, R2T-3): the port calls this on every poll it does not feed the page (the gift card over it,
   the page closed by BOOT twice or a birth) - the press is forgotten, so the next feed's "up" is no release of it (CHECK FOR
   UPDATES fired as the card went, or the next time the page opened). An armed SURE? stays armed: only the press goes */
void updates_page_cancel(void) { s_down = false; s_hit = UPD_TAP_NONE; }
int updates_page_touch(float x, float y, bool down, int64_t now_us) {
    int r = UPD_TAP_NONE;
    if (down) { s_lx = x; s_ly = y; }
    if (down && !s_down) {
        s_px = x; s_py = y; s_hit = updates_page_tap(x, y); s_press_us = now_us;
        if (s_face_on && hold_spent_us_at(&s_face_at, now_us, now_us, OPEN_WAIT_US)) s_hit = UPD_TAP_NONE;   /* the face just turned: its tap's double */
    } else if (!down && s_down) {
        x = s_lx; y = s_ly;
        int h = updates_page_tap(x, y);
        float dx = x - s_px, dy = y - s_py;
        const bool tap = h && h == s_hit && dx * dx + dy * dy < 24 * 24;
        if (tap && h >= UPD_TAP_FORGET) {
            const int row = h - UPD_TAP_FORGET;
            if (updates_forget_row() != row) {                                        /* SURE? - on this row's network (another's goes out) */
                if (net_port_creds_at(row, s_forget_ssid, NULL)) s_forget_at = now_us; else s_forget_ssid[0] = 0;
            } else if (!hold_spent_us_at(&s_forget_at, s_press_us, now_us, OPEN_WAIT_US)) {   /* (spent: the tap before's double) */
                net_port_creds_forget_one(s_forget_ssid); s_forget_ssid[0] = 0; r = h;   /* this one; the others stay, in their order */
            }
        } else {
            s_forget_ssid[0] = 0;                                                    /* anything else: not armed */
            if (tap) r = h;
            if (r == UPD_TAP_NETS || r == UPD_TAP_NETS_BACK) { s_nets = r == UPD_TAP_NETS; s_face_at = now_us; s_face_on = true; }   /* the face turns */
        }
    }
    s_down = down;
    return r;
}

/* ====================================================================
 * 2. UPDATE MODE
 * ==================================================================== */
enum { STEP_NONE = 0, STEP_POWER, STEP_SCANNING, STEP_LIST, STEP_PASSWORD, STEP_CONNECTING, STEP_CHECKING,
       STEP_OFFER, STEP_UP_TO_DATE, STEP_INSTALLING, STEP_MESSAGE, STEP_RESTART,
       STEP_SEEKING };                       /* 0.4.2: the saved network tried is not in range - a scan, for the other saved ones */
/* what the message page's buttons do */
enum { ACT_NONE = 0, ACT_RETRY_CONNECT, ACT_RETYPE, ACT_SCAN, ACT_RETRY_CHECK, ACT_RETRY_INSTALL,
       ACT_REPLACE_OK, ACT_LIST };           /* 0.4.2, THREE NETWORKS SAVED: on with the network picked / back to the list */
/* hit ids */
enum { HIT_NONE = 0, HIT_BACK, HIT_CANCEL, HIT_RESCAN, HIT_YES, HIT_NO, HIT_PRIMARY, HIT_SECONDARY, HIT_TERTIARY,
       HIT_PG_UP, HIT_PG_DOWN, HIT_ROW0 = 200, HIT_KEY0 = 100 };

static struct {
    int  step, outcome, page;
    int  battery_pct; bool on_power;
    bool add;                                /* 0.4.2: begun by ADD A NETWORK (update_begin_add), not CHECK FOR UPDATES - the PLUG IN page's words */
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    bool saved;                              /* the network came from the store */
    bool secured;
    bool fresh;                              /* the network picked is not the one last typed for: its keyboard starts empty */
    bool sought; wifi_names_t tried;         /* 0.4.2: the scan for the saved networks has run; the ones tried SINCE that scan, by name */
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
    float hold_t0; bool holding;             /* the OFFER's UPDATE held (Strato 11-06: an install has no way back - a 1 s hold, not a tap) */
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
static void draw_field(uint16_t *fb, int stride, double clock) {    /* the typed passphrase, its tail if long, a cursor */
    render_rect(fb, stride, UX, FIELD_Y, UW, FIELD_H, C_PANEL); render_rect_edge(fb, stride, UX, FIELD_Y, UW, FIELD_H, C_INNER);
    const char *p = s.pass; size_t n = strlen(p);
    char shown[FIELD_VISIBLE + 2];
    if (n > FIELD_VISIBLE) { snprintf(shown, sizeof shown, "%c%s", '<', p + n - (FIELD_VISIBLE - 1)); p = shown; }
    int x = UX + 10, y = FIELD_Y + (FIELD_H - 14) / 2;
    update_text(fb, stride, x, y, 2, C_TEXT, p);
    if ((int)(clock * 2) & 1) render_rect(fb, stride, x + update_text_w(p, 2) + (*p ? 4 : 0), y, 2, 14, C_EDGE);
    char count[12]; snprintf(count, sizeof count, "%d", (int)n);
    render_text(fb, stride, UX + UW - render_text_w(count, 1) - 8, FIELD_Y - 10, 1, C_QUIET, count);
}

/* ---- the pages ---- */

static void go(int step, int page) { s.step = step; s.page = page; s.t = 0; s.hit = HIT_NONE; s.holding = false; }
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
const char *update_message_line1(void) { return s.line1; }
const char *update_ssid(void) { return s.ssid; }
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
static void type_password(bool fresh);
/* SEVERAL SAVED NETWORKS (0.4.2, wifi_nets.h). The one used last is tried first, straight away, as when the tank
 * kept one - nothing is slower for it. Only when it is NOT IN RANGE does the tank look (one scan) and join the
 * first of its list that the radio sees; one seen that still does not answer is passed over for the next; one that
 * refuses its password stops there with NOT ACCEPTED (TYPE IT AGAIN is the way on). None left: NO SIGNAL.
 * The try BEFORE the scan is not held against the one used last: a first sweep misses a router now and then, and
 * if the scan then hears it, it is tried once more (it is first in the list) before the others. */
static void seek_pick(void) {
    if (net_port_scan_saved(&s.tried, s.ssid, s.pass) >= 0) { s.secured = s.pass[0] != 0; start_connect(); return; }
    net_port_creds_get(s.ssid, s.pass); s.secured = s.pass[0] != 0;   /* TRY AGAIN starts over, from the one used last */
    s.sought = false; wifi_names_clear(&s.tried);
    message("NO SIGNAL", "NO SAVED NETWORK", false, "IS IN RANGE", "TRY AGAIN", ACT_RETRY_CONNECT, "OTHER NETWORK", ACT_SCAN);
}
static void seek_next(void) {
    if (s.sought) { wifi_names_add(&s.tried, s.ssid); seek_pick(); return; }   /* (the scan's answer stands for the rest of the list) */
    s.sought = true; wifi_names_clear(&s.tried);
    net_port_scan_start(); go(STEP_SEEKING, UPD_PG_BUSY); busy("LOOKING FOR NETWORKS");
}
/* the network picked from the list: its keyboard, or straight on if it is open */
static void picked_go(void) { if (s.secured) type_password(s.fresh); else { s.pass[0] = 0; start_connect(); } }
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
static void begin(int battery_pct, bool on_power, bool add) {
    memset(&s, 0, sizeof s);
    s.battery_pct = battery_pct; s.on_power = on_power; s.add = add;
    if (!on_power && battery_pct >= 0 && battery_pct < UPD_BATTERY_MIN_PCT) { go(STEP_POWER, UPD_PG_POWER); return; }
    if (!add && net_port_creds_get(s.ssid, s.pass)) { s.saved = true; s.secured = s.pass[0] != 0; start_connect(); }
    else start_scan();
}
void update_begin(int battery_pct, bool on_power) { begin(battery_pct, on_power, false); }
/* ADD A NETWORK (0.4.2, the WI-FI page): the wizard from its list, whatever is saved - the page OTHER NETWORK always led
   to. A fourth still says THREE NETWORKS SAVED first; one already saved takes the new password and no new place */
void update_begin_add(int battery_pct, bool on_power) { begin(battery_pct, on_power, true); }
bool update_active(void) { return s.step != STEP_NONE; }
int  update_outcome(void) { return s.outcome; }
int  update_page(void) { return s.page; }
const char *update_typed(void) { return s.pass; }
/* the PLUG IN page's words. ADD A NETWORK is behind the same battery gate (the radio is the cost, not the download), but it
   is no update: its page does not say UPDATE (the 0.4.2 review; "ADDING A NETWORK NEEDS THE CABLE IN" is 418 px, wider
   than the watch's glass) */
const char *update_power_title(void) { return s.add ? "PLUG IN FIRST" : "PLUG IN TO UPDATE"; }
const char *update_power_line(void)  { return s.add ? "THIS NEEDS THE CABLE IN" : "AN UPDATE NEEDS THE CABLE IN"; }

static void fail(int err) {
    switch (err) {
    case NET_ERR_NO_SIGNAL: message("NO SIGNAL", s.ssid, true, "IS NOT IN RANGE", "TRY AGAIN", ACT_RETRY_CONNECT, "OTHER NETWORK", ACT_SCAN); break;
    case NET_ERR_PASSWORD:  message3("NOT ACCEPTED", s.ssid, true, "DID NOT TAKE THE PASSWORD",   /* (0.4.2: it said "<name> / REFUSE THE FIRST TRIES. TRY AGAIN?",
                                        34 letters - wider than the watch's glass; TRY AGAIN is still the first button, for the routers that refuse a first try) */
                                     "TRY AGAIN", ACT_RETRY_CONNECT, "TYPE IT AGAIN", ACT_RETYPE, "OTHER NETWORK", ACT_SCAN); break;
    case NET_ERR_NO_NET:    message("NO ANSWER", "THE UPDATE SERVER DID NOT", false, "ANSWER. IS THE INTERNET UP?", "TRY AGAIN", ACT_RETRY_CHECK, "OTHER NETWORK", ACT_SCAN); break;
    case NET_ERR_BAD_MANIFEST: message("ODD ANSWER", "THE UPDATE SERVER SENT", false, "SOMETHING THIS TANK CANNOT READ", "TRY AGAIN", ACT_RETRY_CHECK, NULL, 0); break;
    case NET_ERR_DOWNLOAD:  message("DOWNLOAD STOPPED", "NOTHING HAS CHANGED", false, "", "TRY AGAIN", ACT_RETRY_INSTALL, NULL, 0); break;
    case NET_ERR_VERIFY:    message("NOT VERIFIED", "THE UPDATE WAS NOT SIGNED", false, "BY POCKET TANK. NOTHING CHANGED", NULL, 0, NULL, 0); break;
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
    case ACT_REPLACE_OK:    picked_go(); break;
    case ACT_LIST:          if (s.n_aps) go(STEP_LIST, UPD_PG_SCAN); else start_scan(); break;
    default:                leave(UPD_BACK); break;
    }
}
static bool newer(const update_manifest_t *m) { return m->release_num > (uint32_t)PT_RELEASE_NUM; }
/* the board whose version this tank should run (0.3.9.3): the board it IS, by
 * the boot's chip probe, else this build's own. They differ when another
 * board's version was installed (the 1.8's runs on every board): its own
 * version is offered then, at any release, as long as the cable is not needed */
const char *update_target_board(void) { const char *hw = board_port_hw(); return hw ? hw : PT_BOARD; }
static bool crossgrade(void) { return strcmp(update_target_board(), PT_BOARD) != 0; }
/* the save-format floor, in this build's packing: a manifest written before
 * 0.3.8.1 packed three bytes (version.h), and 0x10000 is where four-byte
 * numbers begin (0.1.0.0 would already be 0x00010000) */
static uint32_t min_from_num(const update_manifest_t *m) { return m->min_from && m->min_from < 0x10000u ? m->min_from << 8 : m->min_from; }

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
        } else if (st == NET_FAILED) {                               /* the store is kept: routers refuse first tries, TYPE IT AGAIN replaces it */
            const int err = net_port_fail_reason();
            if (s.saved && err == NET_ERR_NO_SIGNAL && net_port_creds_count() > 1) seek_next();   /* not in range: another saved one? */
            else fail(err);
        }
        break;
    case STEP_SEEKING:
        st = net_port_scan_state();
        if (st == NET_DONE) seek_pick();
        else if (st == NET_FAILED) fail(net_port_fail_reason());
        break;
    case STEP_CHECKING: {
        const update_manifest_t *m = NULL;
        st = net_port_check_state(&m);
        if (st == NET_DONE && m) {
            s.m = *m; s.have_m = true;
            if (strcmp(m->board, update_target_board()) != 0)   /* another board's manifest (2026-10-02): never offered */
                message("WRONG BOARD", "THIS UPDATE IS FOR", false, "ANOTHER BOARD. NOTHING CHANGED", NULL, 0, NULL, 0);
            else if (!crossgrade() && !newer(m)) { message("UP TO DATE", "YOUR TANK HAS THE LATEST", false, "VERSION", NULL, 0, NULL, 0); s.step = STEP_UP_TO_DATE; }
            else if (m->needs_cable || (min_from_num(m) && min_from_num(m) > (uint32_t)PT_RELEASE_NUM))
                message("NEEDS THE CABLE", "THIS UPDATE IS INSTALLED", false, "FROM POCKETANK.COM/INSTALL", NULL, 0, NULL, 0);
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
            snprintf(s.ssid, sizeof s.ssid, "%s", ap->ssid); s.secured = ap->secured; s.saved = false; s.fresh = !same;
            /* THREE SAVED AND THIS IS A FOURTH (0.4.2): said before a letter is typed, with the name that goes - the
               one used longest ago. OK goes on (it is replaced when the new one has joined, not before); OTHER
               NETWORK is the list again; BACK TO TANK leaves all three (FORGET on the UPDATES page picks another) */
            char victim[NET_SSID_MAX + 1];
            if (net_port_creds_replaces(s.ssid, victim))
                message("THREE NETWORKS SAVED", victim, true, "WILL BE FORGOTTEN TO MAKE ROOM", "OK", ACT_REPLACE_OK, "OTHER NETWORK", ACT_LIST);
            else picked_go();
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
    if (down && !s.down) { s.px = x; s.py = y; s.hit = update_hit(x, y);
                           if (s.page == UPD_PG_OFFER && s.hit == HIT_YES) { s.holding = true; s.hold_t0 = s.t; } }   /* (a press from before the
                                                                   page came is no press here: go() forgot it - THE TOUCH RULE's G1) */
    else if (down && s.page == UPD_PG_OFFER && s.hit == HIT_YES) {   /* UPDATE HELD (a press begun on it): it installs once the hold is
                                                           full; slid off, the fill empties, and back on it starts again from empty */
        if (update_hit(x, y) != HIT_YES) s.holding = false;
        else if (!s.holding) { s.holding = true; s.hold_t0 = s.t; }
        else if (s.t - s.hold_t0 >= UPD_HOLD_S) { s.holding = false; s.hit = HIT_NONE; start_install(); }
    }
    else if (!down && s.down) {
        x = s.lx; y = s.ly;                              /* where the finger was, not where "nothing" is */
        int h = update_hit(x, y);
        float dx = x - s.px, dy = y - s.py;
        if (h && h == s.hit && dx * dx + dy * dy < 30 * 30 && !(s.page == UPD_PG_OFFER && h == HIT_YES)) activate(h);   /* (UPDATE: its hold, not a tap) */
        s.hit = HIT_NONE; s.holding = false;
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
static void spinner(uint16_t *fb, int stride, int cx, int cy, double clock) {
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
void render_clock_sync(uint16_t *fb, int stride, double clock) {   /* the loading screen's look (10-31): the tank's WAKING THE TANK follows on the same water */
    render_loading(fb, stride, "WAKING UP", clock, "CHECKING THE TIME");
}
void render_update(uint16_t *fb, int stride, double clock) {
    page_bg(fb, stride);
    char line[80];
    switch (s.page) {
    case UPD_PG_POWER:
        text_c(fb, stride, 96, 3, C_TEXT, update_power_title());
        snprintf(line, sizeof line, "THE BATTERY IS AT %d%%", s.battery_pct);
        text_c(fb, stride, 150, 2, C_CAPT, line);
        text_c(fb, stride, 176, 2, C_QUIET, update_power_line());
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
            else text_c(fb, stride, 252, 2, C_QUIET, "KEEP THE TANK ON");
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
            if (!s.aps[i].secured) render_text(fb, stride, UX + UW - (pages > 1 ? 36 : 0) - 44 - 8 - render_text_w("OPEN", 1), y + 12, 1, C_QUIET, "OPEN");
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
        const char *own = crossgrade() ? pt_board_words(update_target_board()) : NULL;   /* this board's own version (0.3.9.3) */
        if (own) snprintf(line, sizeof line, "%s VERSION", own);
        text_c(fb, stride, 52, 3, C_TEXT, own ? line : "UPDATE READY");
        uint32_t bytes = s.m.app_size + (s.m.model_offered ? s.m.model_size : 0);
        snprintf(line, sizeof line, "VERSION %s %s, %.1f MB", s.m.release, PT_RELEASE_STAGE, bytes / 1048576.0);
        text_c(fb, stride, 92, 2, C_CAPT, line);
        char l1[32], l2[32];
        if (own) { snprintf(l1, sizeof l1, "MADE FOR THIS BOARD"); snprintf(l2, sizeof l2, "YOUR FISH STAY"); }
        else wrap2(s.m.note, l1, l2, sizeof l1);
        text_c(fb, stride, 138, 2, C_TEXT, l1);
        if (l2[0]) text_c(fb, stride, 160, 2, C_TEXT, l2);
        int secs = (int)(bytes / 120000) + 10;
        if (secs < 90) snprintf(line, sizeof line, "IT TAKES ABOUT A MINUTE");
        else snprintf(line, sizeof line, "IT TAKES ABOUT %d MINUTES", (secs + 30) / 60);
        text_c(fb, stride, 208, 2, C_QUIET, line);
        text_c(fb, stride, 230, 2, C_QUIET, "THE TANK RESTARTS BY ITSELF");
        {   /* HOLD TO UPDATE (Strato 11-06): the fill grows across the button as the finger stays, as REMOVE's HOLD TO SELL */
            const char *lb = "HOLD TO UPDATE";
            float f = s.holding ? (s.t - s.hold_t0) / UPD_HOLD_S : 0; if (f > 1) f = 1;
            render_rect(fb, stride, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_GO);
            if (f > 0) render_rect(fb, stride, UPD_BTN_L_X, UPD_BTN_Y, (int)(UPD_BTN_W * f), UPD_BTN_H, C_FILL);
            render_rect_edge(fb, stride, UPD_BTN_L_X, UPD_BTN_Y, UPD_BTN_W, UPD_BTN_H, C_GO_E);
            if (PAGE_BOWL) {                                 /* (the bowl's button is narrower: two lines, Strato 11-06) */
                render_text(fb, stride, UPD_BTN_L_X + (UPD_BTN_W - render_text_w("HOLD TO", 2)) / 2, UPD_BTN_Y + 3, 2, C_TEXT, "HOLD TO");
                render_text(fb, stride, UPD_BTN_L_X + (UPD_BTN_W - render_text_w("UPDATE", 2)) / 2, UPD_BTN_Y + UPD_BTN_H - 17, 2, C_TEXT, "UPDATE");
            } else render_text(fb, stride, UPD_BTN_L_X + (UPD_BTN_W - render_text_w(lb, 2)) / 2, UPD_BTN_Y + (UPD_BTN_H - 14) / 2, 2, C_TEXT, lb);
        }
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
        text_c(fb, stride, 96, 3, C_TEXT, s.title);                    /* (0.4.2: the busy pages' title line - it stood 12 px higher - and their first line's) */
        if (s.line1_ascii) atext_c(fb, stride, 134, 2, C_CAPT, s.line1); else text_c(fb, stride, 134, 2, C_CAPT, s.line1);
        if (s.line2[0]) text_c(fb, stride, 156, 2, C_CAPT, s.line2);
        if (s.step == STEP_UP_TO_DATE) {
            snprintf(line, sizeof line, "VERSION %s %s", PT_RELEASE, PT_RELEASE_STAGE); text_c(fb, stride, 190, 2, C_QUIET, line);
            float left = 1.0f - s.t / UPD_UP_TO_DATE_S; if (left < 0) left = 0;
            render_rect(fb, stride, UX, 224, (int)(UW * left), 2, C_INNER);
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
