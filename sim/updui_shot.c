#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* updui_shot.c - --updui-shot <prefix> (0.4.2, the UPDATES page made plain): every state of the UPDATES page in the
 * tank and of update mode's pages, on this board, as <prefix>_<nn><name>.ppm - the same states before and after the
 * redesign, so the two contact sheets (tools/updui042_sheets.py -> sim/sheets/updui042_<board>_before / _after.png)
 * stand side by side. Nothing is tested here: --selftest-update, --selftest-wifi3 and --selftest-updui do that. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tank.h"
#include "render.h"
#include "update.h"
#include "wifi_nets.h"
#include "../firmware/main/update_req.h"   /* the request's words (H) */

void net_sim_advance(float dt);
void net_sim_see(const char *const *names, int n);
void net_sim_cut_at(int k);
void net_sim_cold(int n);

#define PGX(x) (PAGE_X + (x))
#define PGY(y) (PAGE_Y + (y))
#define ADV(secs) do { net_sim_advance(secs); update_tick(secs); } while (0)
#define TAP(X, Y) (update_touch(PGX(X), PGY(Y), true), update_touch(PGX(X), PGY(Y), false))
#define KEY(cell) TAP(UPD_KB_X + ((cell) % UPD_KB_COLS) * UPD_KB_PX + UPD_KB_W / 2, UPD_KB_Y + ((cell) / UPD_KB_COLS) * UPD_KB_PY + UPD_KB_H / 2)
#define ROWY(r)   (UPD_ROW_Y0 + (r) * UPD_ROW_H + 16)
static const char LONG32[] = "The Longest Network Name Is-32ch";
static uint16_t fb[TANK_W * TANK_H];
static const char *s_prefix; static int s_n;
static void ppm(const char *name) {
    char path[700]; snprintf(path, sizeof path, "%s_%02d%s.ppm", s_prefix, ++s_n, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t v = FB_SWAP16(fb[i]);
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
static void store_reset(void) { net_sim_cut_at(0); net_sim_cold(0); net_sim_see(NULL, 0); net_port_creds_forget(); }
#define JOINED() do { update_begin(50, false); ADV(2); ADV(2); net_port_off(); } while (0)   /* the first one joined */
#define MODE(name) do { render_update(fb, TANK_W, 0.3f); ppm(name); } while (0)
#define HOLD_UPDATE() do { update_touch(PGX(UPD_BTN_L_X + UPD_BTN_W / 2), PGY(UPD_BTN_Y + UPD_BTN_H / 2), true); \
                           for (int hk_ = 0; hk_ < 12; hk_++) { update_tick(0.1f); update_touch(PGX(UPD_BTN_L_X + UPD_BTN_W / 2), PGY(UPD_BTN_Y + UPD_BTN_H / 2), true); } \
                           update_touch(PGX(UPD_BTN_L_X + UPD_BTN_W / 2), PGY(UPD_BTN_Y + UPD_BTN_H / 2), false); } while (0)

/* the pages in the tank, for the networks saved now: before the redesign one page, after it UPDATES and its WI-FI page */
static void tank_pages(const char *name, bool armed_row1);

int updui_shot(const char *prefix) {
    s_prefix = prefix; s_n = 0;
    { static char wf[640]; const char *sv = getenv("POCKET_TANK_SAVE"); snprintf(wf, sizeof wf, "%s.updui.wifi", sv && sv[0] ? sv : "/tmp/pt-updui"); setenv("POCKET_TANK_WIFI", wf, 1); }
    unsetenv("POCKET_TANK_FAKE_UPDATE"); unsetenv("POCKET_TANK_CREW");
    setenv("POCKET_TANK_BOARD_CODE", "KM4T-7RXA", 1);
    store_reset();
    /* ---- in the tank ---- */
    tank_pages("none", false);
    net_port_creds_set("Fishbowl", "password-1");
    tank_pages("one_new", false);                                          /* saved, never joined yet */
    JOINED();
    tank_pages("one", false);
    net_port_creds_set("Strato's Wi-Fi", "password-1"); JOINED();
    tank_pages("two", false);
    net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1"); JOINED();
    tank_pages("three", false);
    tank_pages("three_sure", true);
    net_port_creds_forget_one("Fishbowl"); net_port_creds_set(LONG32, "password-1"); JOINED();
    setenv("POCKET_TANK_CREW", "1", 1);
    tank_pages("name32_crew", false);
    unsetenv("POCKET_TANK_CREW");
    /* ---- update mode ---- */
    store_reset();
    update_begin(12, false); MODE("plug_in");
    update_begin_add(12, false); MODE("plug_in_add");                      /* (ADD A NETWORK under the battery gate: its own words) */
    update_begin(50, false); ADV(0.4f); MODE("looking");
    ADV(1.0f); MODE("list");
    TAP(PAGE_W / 4, ROWY(1));                                              /* Strato's Wi-Fi: its keyboard */
    KEY(7); KEY(4); KEY(11); KEY(11); MODE("password");                    /* h e l l */
    KEY(14 + 4); KEY(0); KEY(1); KEY(2); KEY(3); KEY(14 + 6); ADV(0.4f); MODE("connecting");   /* 123, 0123, JOIN */
    ADV(1.5f); ADV(0.3f); MODE("checking");
    ADV(1.0f); MODE("offer");
    HOLD_UPDATE(); ADV(2.4f); MODE("downloading");
    ADV(4.0f); ADV(0.2f); MODE("installed");
    net_port_off();
    setenv("POCKET_TANK_FAKE_UPDATE", "none", 1); update_begin(50, false); ADV(2); ADV(2); MODE("up_to_date");
    setenv("POCKET_TANK_FAKE_UPDATE", "cable", 1); update_begin(50, false); ADV(2); ADV(2); MODE("needs_cable");
    setenv("POCKET_TANK_FAKE_UPDATE", "fail", 1); update_begin(50, false); ADV(2); ADV(2); MODE("no_answer");
    setenv("POCKET_TANK_FAKE_UPDATE", "downloadfail", 1); update_begin(50, false); ADV(2); ADV(2); HOLD_UPDATE(); ADV(3); MODE("download_stopped");
    unsetenv("POCKET_TANK_FAKE_UPDATE");
    update_begin(50, false); ADV(2); ADV(2); HOLD_UPDATE(); ADV(2);
    TAP(UPD_BTN_MID_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2); MODE("canceled");
    net_port_off(); net_port_creds_forget();
    net_port_creds_set("Ghost", "password-1"); update_begin(50, false); ADV(2); MODE("no_signal");
    net_port_off(); net_port_creds_forget();
    net_port_creds_set("Strato's Wi-Fi", "wrong-now"); update_begin(50, false); ADV(2); MODE("not_accepted");
    net_port_off(); net_port_creds_forget();
    net_port_creds_set("Fishbowl", "password-1"); net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1");
    { static const char *const see[] = { "Lakeside Cabin", "Neighbor 5G", "CoffeeShop Guest" }; net_sim_see(see, 3); }
    update_begin(50, false); ADV(2); ADV(1.5f); MODE("none_in_range");
    TAP(UPD_BTN_R_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2 - 50); ADV(2); TAP(PAGE_W / 4, ROWY(0)); MODE("three_saved");
    net_port_off();
    net_port_creds_forget(); net_port_creds_set(LONG32, "password-1"); net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1");
    update_begin(50, false); ADV(2); ADV(1.5f); TAP(UPD_BTN_R_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2 - 50); ADV(2); TAP(PAGE_W / 4, ROWY(0)); MODE("three_saved_name32");
    net_port_off(); store_reset();
    { char p[700]; snprintf(p, sizeof p, "%s", getenv("POCKET_TANK_WIFI")); remove(p); strncat(p, ".nets", sizeof p - strlen(p) - 1); remove(p); }
    printf("updui-shot: %d x %d, %d screens -> %s_*.ppm\n", TANK_W, TANK_H, s_n, prefix);
    return 0;
}

static void tank_pages(const char *name, bool armed_row1) {
    char nm[64];
    updates_page_open();
    render_updates_page(fb, TANK_W); snprintf(nm, sizeof nm, "a_updates_%s", name); ppm(nm);
    updates_page_show_nets(true);
    if (armed_row1) {
        const float x = PGX(UPD_FORGET_X + UPD_FORGET_W / 2), y = PGY(UPD_FORGET_Y + UPD_NET_DY + UPD_FORGET_H / 2);
        updates_page_touch(x, y, true, 1000000); updates_page_touch(x, y, false, 1080000);
    }
    render_updates_page(fb, TANK_W); snprintf(nm, sizeof nm, "b_wifi_%s", name); ppm(nm);
    updates_page_open();
}

/* ==================================================================================================================
 * --selftest-updui (0.4.2): THE UPDATES PAGE'S TWO FACES (update.h).
 *   A  one size of everything: the wide buttons, the rows and a FORGET are one height; both faces keep one left edge
 *      and one width; BACK is the same box on both
 *   B  each control answers where it is drawn (its middle and just inside its four corners) and nowhere reads as two;
 *      the UPDATES face has no FORGET and no ADD, the WI-FI face no CHECK
 *   C  on this board's glass: both faces, with none / one / two / three saved, one armed, a 32-character name, the crew's
 *      words - nothing drawn off the glass or within 6 px of its edge, nothing outside the faces' one column
 *   D  the way round: the door opens WI-FI on its tap's lift (not the press), BACK comes back to UPDATES, BACK there is
 *      the port's (UPD_TAP_CLOSE); the page opening again is the UPDATES face
 *   E  THE TOUCH RULE at the turn: the door's double (a second press inside OPEN_WAIT_US, swept over every point of the
 *      door) is no ADD A NETWORK and no FORGET; WI-FI's BACK doubled does not close UPDATES too; a press 0.25 s after
 *      the turn acts at once (no wait added); a press cut by the port (updates_page_cancel) is no tap
 *   F  SURE? does not outlive its page: armed, BACK, the door again - FORGET reads FORGET, nothing forgotten
 *   G  ADD A NETWORK (update_begin_add): with a network saved it begins at the list (never CONNECTING to the saved one);
 *      a new one picked, typed and joined is saved beside the others and the check follows; with three saved a fourth
 *      says THREE NETWORKS SAVED first; update_begin itself as before. Below the battery gate it is PLUG IN FIRST / THIS
 *      NEEDS THE CABLE IN (no "UPDATE": it is none), CHECK FOR UPDATES' page its own words as before, both inside the
 *      glass. With nothing saved; BACK TO TANK from the list (the store untouched to the byte); a name already saved
 *      picked - a wrong password leaves the store as it was, the right one makes no second entry; a check, an add, a
 *      check in one run: no stale add
 *   H  the request's words (firmware/main/update_req.h): CHECK's word unchanged, ADD's its own, each good for one boot,
 *      noise and mismatched words no request, add never left over
 *   I  the longest build ids (a dirty tree's, an early build's, the app description's 31 characters): the value in its
 *      column when it fits, else ending at the column's edge and clear of its label, else cut head...tail; drawn inside
 *      the glass on this board */
#define UF(...) do { printf("FAIL: updui: "); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)
_Static_assert(UPD_CHECK_H == UPD_BTN40 && UPD_DOOR_H == UPD_BTN40 && UPD_NET_H == UPD_BTN40 && UPD_FORGET_H == UPD_BTN40, "one button height");
_Static_assert(UPD_CHECK_X == UPD_X && UPD_CHECK_W == UPD_W && UPD_FORGET_X + UPD_FORGET_W == UPD_X + UPD_W, "one column");
_Static_assert(UPD_NET_DY > UPD_NET_H && UPD_NET_Y0 + 2 * UPD_NET_DY + UPD_NET_H < UPD_LEGEND_Y && UPD_LEGEND_Y + 14 < UPD_ADD_Y && UPD_ADD_Y + UPD_BTN40 < UPD_CLOSE_Y, "the WI-FI face's rows in order");
_Static_assert(UPD_INFO_Y + 2 * UPD_INFO_DY + 14 < UPD_CHECK_Y && UPD_CHECK_Y + UPD_CHECK_H < UPD_TEXT_Y && UPD_TEXT_Y + 34 < UPD_DOOR_Y && UPD_DOOR_Y + UPD_DOOR_H < UPD_CLOSE_Y, "the UPDATES face's rows in order");
static bool u_on_glass(int x, int y, int in) {
    return y >= in && y < TANK_H - in && x >= tank_glass_x0((float)y) + in && x <= tank_glass_x1((float)y) - in && y >= tank_glass_top((float)x) + in;
}
static const char *u_fits(int *bx, int *by) {
    const uint16_t ink = fb[0];
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
        if (fb[y * TANK_W + x] == ink) continue;
        *bx = x; *by = y;
        if (!u_on_glass(x, y, 6)) return "off the glass or within 6 px of its edge";
        if (x < PGX(UPD_X) || x >= PGX(UPD_X + UPD_W)) return "outside the page's column";
    }
    return NULL;
}
static const char *u_fits_mode(int *bx, int *by) {                 /* an update mode page: on the glass, 6 px in, inside its panel */
    const uint16_t ink = fb[0];
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
        if (fb[y * TANK_W + x] == ink) continue;
        *bx = x; *by = y;
        if (!u_on_glass(x, y, 6)) return "off the glass or within 6 px of its edge";
        if (x < PGX(32) || x >= PGX(32 + UPD_PANEL_W)) return "outside the panel";
    }
    return NULL;
}
static int u_box(const char *name, int bx, int by, int bw, int bh, int want) {
    const int px[5] = { bx + bw / 2, bx + 1, bx + bw - 2, bx + 1, bx + bw - 2 }, py[5] = { by + bh / 2, by + 1, by + 1, by + bh - 2, by + bh - 2 };
    for (int k = 0; k < 5; k++) { const int h = updates_page_tap(PGX(px[k]), PGY(py[k])); if (h != want) { printf("FAIL: updui: B: %s at page %d,%d reads %d (want %d)\n", name, px[k], py[k], h, want); return 1; } }
    return 0;
}
#define UT(X, Y, d_us, u_us) (updates_page_touch(PGX(X), PGY(Y), true, d_us), updates_page_touch(PGX(X), PGY(Y), false, u_us))
int selftest_updui(void) {
    { static char wf[640]; const char *sv = getenv("POCKET_TANK_SAVE"); snprintf(wf, sizeof wf, "%s.updui-test.wifi", sv && sv[0] ? sv : "/tmp/pt-updui-test"); setenv("POCKET_TANK_WIFI", wf, 1); }
    unsetenv("POCKET_TANK_FAKE_UPDATE"); unsetenv("POCKET_TANK_CREW"); setenv("POCKET_TANK_BOARD_CODE", "KM4T-7RXA", 1);
    store_reset();
    const int DX = UPD_X + UPD_W / 2, DY = UPD_DOOR_Y + UPD_DOOR_H / 2, BX = UPD_CLOSE_X + UPD_CLOSE_W / 2, BY = UPD_CLOSE_Y + UPD_CLOSE_H / 2, AY = UPD_ADD_Y + UPD_BTN40 / 2;
    /* ---- B ---- */
    net_port_creds_set("Fishbowl", "password-1"); net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1");
    updates_page_open();
    if (updates_page_nets()) UF("B: the page opens on the WI-FI face");
    if (u_box("CHECK FOR UPDATES", UPD_CHECK_X, UPD_CHECK_Y, UPD_CHECK_W, UPD_CHECK_H, UPD_TAP_CHECK) || u_box("the WI-FI door", UPD_X, UPD_DOOR_Y, UPD_W, UPD_DOOR_H, UPD_TAP_NETS)
        || u_box("BACK", UPD_CLOSE_X, UPD_CLOSE_Y, UPD_CLOSE_W, UPD_CLOSE_H, UPD_TAP_CLOSE)) return 1;
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) { const int h = updates_page_tap((float)x, (float)y);
        if (h >= UPD_TAP_FORGET || h == UPD_TAP_ADD || h == UPD_TAP_NETS_BACK) UF("B: the UPDATES face reads %d at frame %d,%d", h, x, y); }
    updates_page_show_nets(true);
    for (int r = 0; r < 3; r++) if (u_box("a FORGET", UPD_FORGET_X, UPD_FORGET_Y + r * UPD_NET_DY, UPD_FORGET_W, UPD_FORGET_H, UPD_TAP_FORGET + r)) return 1;
    if (u_box("ADD A NETWORK", UPD_X, UPD_ADD_Y, UPD_W, UPD_BTN40, UPD_TAP_ADD) || u_box("WI-FI's BACK", UPD_CLOSE_X, UPD_CLOSE_Y, UPD_CLOSE_W, UPD_CLOSE_H, UPD_TAP_NETS_BACK)) return 1;
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) { const int h = updates_page_tap((float)x, (float)y);
        if (h == UPD_TAP_CHECK || h == UPD_TAP_NETS || h == UPD_TAP_CLOSE) UF("B: the WI-FI face reads %d at frame %d,%d", h, x, y); }
    /* ---- C ---- */
    store_reset();
    for (int n = 0; n <= 5; n++) {
        if (n == 1) net_port_creds_set("Fishbowl", "password-1");
        if (n == 2) { JOINED(); net_port_creds_set("Strato's Wi-Fi", "password-1"); JOINED(); }
        if (n == 3) { net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1"); JOINED(); }
        if (n == 4) { net_port_creds_forget_one("Fishbowl"); net_port_creds_set(LONG32, "password-1"); JOINED(); setenv("POCKET_TANK_CREW", "1", 1); }
        for (int face = 0; face < 2; face++) {
            updates_page_open(); updates_page_show_nets(face == 1);
            if (n == 5 && face == 1) UT(UPD_FORGET_X + UPD_FORGET_W / 2, UPD_FORGET_Y + UPD_FORGET_H / 2, 1000000, 1080000);   /* the long name's FORGET armed */
            render_updates_page(fb, TANK_W);
            int bx = 0, by = 0; const char *bad = u_fits(&bx, &by);
            if (bad) UF("C: the %s face, case %d, draws %s at frame %d,%d", face ? "WI-FI" : "UPDATES", n, bad, bx, by);
        }
    }
    unsetenv("POCKET_TANK_CREW");
    /* ---- D ---- */
    updates_page_open();
    int r = updates_page_touch(PGX(DX), PGY(DY), true, 1000000);
    if (r || updates_page_nets()) UF("D: the door opened on the press (%d)", r);
    r = updates_page_touch(PGX(DX), PGY(DY), false, 1080000);
    if (r != UPD_TAP_NETS || !updates_page_nets()) UF("D: the door's tap -> %d, WI-FI up %d", r, updates_page_nets());
    r = UT(BX, BY, 2000000, 2080000);
    if (r != UPD_TAP_NETS_BACK || updates_page_nets()) UF("D: WI-FI's BACK -> %d, WI-FI still up %d", r, updates_page_nets());
    r = UT(BX, BY, 3000000, 3080000);
    if (r != UPD_TAP_CLOSE) UF("D: the UPDATES face's BACK -> %d (want the port's CLOSE)", r);
    UT(DX, DY, 4000000, 4080000); updates_page_open();
    if (updates_page_nets()) UF("D: the page opened again on the WI-FI face");
    updates_page_touch(PGX(DX), PGY(DY), true, 5000000); r = updates_page_touch(PGX(BX), PGY(BY), true, 5040000); r = updates_page_touch(PGX(BX), PGY(BY), false, 5080000);
    if (r || updates_page_nets()) UF("D: a finger that slid from the door to BACK did something (%d)", r);
    /* ---- E ---- */
    {   int64_t t = 10000000; int swept = 0;
        for (int y = UPD_DOOR_Y - 8; y < UPD_DOOR_Y + UPD_DOOR_H + 8; y += 2) for (int x = UPD_X - 8; x < UPD_X + UPD_W + 8; x += 3) {
            updates_page_open();
            if (updates_page_tap(PGX(x), PGY(y)) != UPD_TAP_NETS) continue;
            const int n0 = net_port_creds_count();
            r = UT(x, y, t, t + 80000); if (r != UPD_TAP_NETS) UF("E: the door at page %d,%d -> %d", x, y, r);
            r = UT(x, y, t + 180000, t + 260000);                    /* its double, 100 ms after the lift */
            if (r || !updates_page_nets() || updates_forget_armed() || net_port_creds_count() != n0) UF("E: the door's double at page %d,%d -> %d (WI-FI up %d, armed %d)", x, y, r, updates_page_nets(), updates_forget_armed());
            r = updates_page_touch(PGX(x), PGY(y), true, t + 270000); r = updates_page_touch(PGX(x), PGY(y), false, t + 900000);   /* ... pressed firmly and slowly: still spent (read at the press) */
            if (r || updates_forget_armed()) UF("E: the door's double, held 0.6 s, at page %d,%d -> %d", x, y, r);
            t += 5000000; swept++;
        }
        if (swept < 100) UF("E: (only %d points of the door swept)", swept);
        updates_page_open(); UT(DX, DY, t, t + 80000);
        r = UT(DX, AY, t + 330000, t + 410000);                      /* 0.25 s after the turn: a tap of its own, at once */
        if (r != UPD_TAP_ADD) UF("E: ADD A NETWORK 0.25 s after the door -> %d (nothing may wait longer than the double)", r);
        t += 5000000;
        updates_page_open(); UT(DX, DY, t, t + 80000); t += 2000000;
        r = UT(BX, BY, t, t + 80000); if (r != UPD_TAP_NETS_BACK) UF("E: (WI-FI's BACK -> %d)", r);
        r = UT(BX, BY, t + 180000, t + 260000);
        if (r || updates_page_nets()) UF("E: WI-FI's BACK doubled closed the UPDATES page too (%d)", r);
        r = UT(BX, BY, t + 600000, t + 680000);
        if (r != UPD_TAP_CLOSE) UF("E: BACK 0.5 s after WI-FI's BACK -> %d", r);
        t += 5000000;
        updates_page_open(); updates_page_show_nets(true);
        updates_page_touch(PGX(DX), PGY(AY), true, t); updates_page_cancel(); r = updates_page_touch(PGX(DX), PGY(AY), false, t + 80000);
        if (r) UF("E: a press the port cut still tapped ADD A NETWORK (%d)", r);
        r = UT(DX, AY, t + 1000000, t + 1080000);
        if (r != UPD_TAP_ADD) UF("E: ADD A NETWORK after a cut press -> %d", r);
    }
    /* ---- F ---- */
    {   const int fx = UPD_FORGET_X + UPD_FORGET_W / 2, fy = UPD_FORGET_Y + UPD_FORGET_H / 2, n0 = net_port_creds_count();
        updates_page_open(); updates_page_show_nets(true);
        UT(fx, fy, 50000000, 50080000); if (!updates_forget_armed()) UF("F: (FORGET did not arm)");
        UT(BX, BY, 51000000, 51080000); if (updates_forget_armed()) UF("F: SURE? stayed armed behind the UPDATES face");
        UT(DX, DY, 52000000, 52080000);
        r = UT(fx, fy, 53000000, 53080000);
        if (r || net_port_creds_count() != n0 || !updates_forget_armed()) UF("F: FORGET after BACK and the door again -> %d, %d saved (want it armed, nothing forgotten)", r, net_port_creds_count());
        r = UT(fx, fy, 54000000, 54080000);
        if (r != UPD_TAP_FORGET || net_port_creds_count() != n0 - 1) UF("F: SURE? -> %d, %d saved", r, net_port_creds_count());
    }
    /* ---- G ---- */
    {   char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        store_reset(); net_port_creds_set("Fishbowl", "password-1");
        update_begin(50, false);
        if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Fishbowl")) UF("G: (update_begin with a saved network is not CONNECTING to it)");
        net_port_off();
        update_begin_add(50, false);
        if (update_page() != UPD_PG_BUSY || update_ssid()[0]) UF("G: ADD A NETWORK began on page %d with '%s' (want LOOKING FOR NETWORKS)", update_page(), update_ssid());
        ADV(1.5f);
        if (update_page() != UPD_PG_SCAN) UF("G: ADD A NETWORK: no list after the scan (page %d)", update_page());
        TAP(PAGE_W / 4, ROWY(1));                                    /* Strato's Wi-Fi */
        if (update_page() != UPD_PG_PASSWORD) UF("G: no keyboard (page %d)", update_page());
        KEY(0); KEY(1); KEY(2); KEY(3); KEY(4); KEY(5); KEY(6); KEY(7); KEY(14 + 6);   /* abcdefgh, JOIN */
        ADV(2);
        if (net_port_creds_count() != 2 || !net_port_creds_get(ssid, pass) || strcmp(ssid, "Strato's Wi-Fi") || strcmp(pass, "abcdefgh")) UF("G: joined: %d saved, first '%s'", net_port_creds_count(), ssid);
        ADV(2);
        if (update_page() != UPD_PG_OFFER) UF("G: no check after the new network joined (page %d)", update_page());
        net_port_off();
        net_port_creds_set("Grandma's House", "password-1");
        update_begin_add(50, false); ADV(1.5f); TAP(PAGE_W / 4, ROWY(3));   /* Apartment 3B: a fourth */
        if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_title(), "THREE NETWORKS SAVED")) UF("G: a fourth from ADD A NETWORK: page %d, '%s'", update_page(), update_message_title());
        TAP(UPD_BTN_MID_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2);
        if (update_outcome() != UPD_BACK || net_port_creds_count() != 3) UF("G: BACK TO TANK from THREE NETWORKS SAVED: outcome %d, %d saved", update_outcome(), net_port_creds_count());
        net_port_off();
        /* the battery gate's page: ADD A NETWORK's own words, CHECK FOR UPDATES' as they were - each fits the glass */
        update_begin_add(12, false);
        if (update_page() != UPD_PG_POWER) UF("G: ADD A NETWORK at 12%% on battery: page %d (want PLUG IN)", update_page());
        if (strcmp(update_power_title(), "PLUG IN FIRST") || strcmp(update_power_line(), "THIS NEEDS THE CABLE IN") || strstr(update_power_title(), "UPDATE") || strstr(update_power_line(), "UPDATE"))
            UF("G: ADD A NETWORK's PLUG IN page says '%s' / '%s' (it is no update)", update_power_title(), update_power_line());
        render_update(fb, TANK_W, 0.3f);
        { int bx = 0, by = 0; const char *bad = u_fits_mode(&bx, &by); if (bad) UF("G: ADD A NETWORK's PLUG IN page draws %s at frame %d,%d", bad, bx, by); }
        TAP(UPD_BTN_MID_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2);
        if (update_outcome() != UPD_BACK) UF("G: ADD A NETWORK's PLUG IN page: BACK TO TANK -> %d", update_outcome());
        update_begin(12, false);
        if (update_page() != UPD_PG_POWER || strcmp(update_power_title(), "PLUG IN TO UPDATE") || strcmp(update_power_line(), "AN UPDATE NEEDS THE CABLE IN"))
            UF("G: CHECK FOR UPDATES' PLUG IN page, after an add: page %d, '%s' / '%s'", update_page(), update_power_title(), update_power_line());
        render_update(fb, TANK_W, 0.3f);
        { int bx = 0, by = 0; const char *bad = u_fits_mode(&bx, &by); if (bad) UF("G: the PLUG IN TO UPDATE page draws %s at frame %d,%d", bad, bx, by); }
        net_port_off();
        /* NOTHING SAVED: the list, a network picked and joined is the first one saved, the check follows */
        store_reset();
        update_begin_add(50, false); ADV(1.5f);
        if (update_page() != UPD_PG_SCAN || net_port_creds_count()) UF("G: ADD A NETWORK with nothing saved: page %d, %d saved", update_page(), net_port_creds_count());
        TAP(PAGE_W / 4, ROWY(0)); KEY(0); KEY(1); KEY(2); KEY(3); KEY(4); KEY(5); KEY(6); KEY(7); KEY(14 + 6); ADV(2);   /* Fishbowl, abcdefgh, JOIN */
        if (net_port_creds_count() != 1 || !net_port_creds_get(ssid, pass) || strcmp(ssid, "Fishbowl") || strcmp(pass, "abcdefgh")) UF("G: the first network added: %d saved, '%s'", net_port_creds_count(), ssid);
        ADV(2); if (update_page() != UPD_PG_OFFER) UF("G: the first network added: no check after it (page %d)", update_page());
        net_port_off();
        /* BACK TO TANK from the list itself: out, the store untouched to the byte */
        net_port_creds_set("Grandma's House", "password-1");
        {   wifi_nets_t was = *net_port_nets();
            update_begin_add(50, false); ADV(1.5f);
            if (update_page() != UPD_PG_SCAN) UF("G: (no list)");
            TAP(UPD_BTN_L_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2);
            if (update_outcome() != UPD_BACK || memcmp(&was, net_port_nets(), sizeof was)) UF("G: BACK TO TANK from ADD A NETWORK's list: outcome %d, the store %s", update_outcome(), memcmp(&was, net_port_nets(), sizeof was) ? "changed" : "kept");
            net_port_off();
            /* A NAME ALREADY SAVED picked from the list. A wrong password: NOT ACCEPTED, the store as it was (its old password
               still the saved one). The right one: that entry takes it - no second entry of the name, the other kept */
            update_begin_add(50, false); ADV(1.5f); TAP(PAGE_W / 4, ROWY(0));   /* Fishbowl again */
            if (update_page() != UPD_PG_PASSWORD) UF("G: a saved name picked: page %d (want its keyboard)", update_page());
            KEY(14); KEY(9); KEY(4); KEY(1); KEY(0); KEY(14); KEY(6); KEY(0); KEY(1); KEY(2);   /* MORE, w r o n, MORE, g a b c */
            if (strcmp(update_typed(), "wrongabc")) UF("G: (typed '%s')", update_typed());
            KEY(14 + 6); ADV(2);
            if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_title(), "NOT ACCEPTED") || memcmp(&was, net_port_nets(), sizeof was))
                UF("G: a saved name with a wrong password: page %d '%s', the store %s", update_page(), update_message_title(), memcmp(&was, net_port_nets(), sizeof was) ? "CHANGED" : "kept");
            TAP(UPD_BTN_R_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2);          /* BACK TO TANK (beside OTHER NETWORK) */
            if (update_outcome() != UPD_BACK || memcmp(&was, net_port_nets(), sizeof was)) UF("G: BACK TO TANK from NOT ACCEPTED: outcome %d, the store %s", update_outcome(), memcmp(&was, net_port_nets(), sizeof was) ? "CHANGED" : "kept");
            net_port_off();
            update_begin_add(50, false); ADV(1.5f); TAP(PAGE_W / 4, ROWY(0));
            KEY(7); KEY(6); KEY(5); KEY(4); KEY(3); KEY(2); KEY(1); KEY(0); KEY(14 + 6); ADV(2);   /* hgfedcba, JOIN */
            const wifi_nets_t *w = net_port_nets(); int fish = 0;
            for (int i = 0; i < w->n; i++) fish += !strcmp(w->net[i].ssid, "Fishbowl");
            if (w->n != 2 || fish != 1 || wifi_nets_find(w, "Grandma's House") < 0 || !net_port_creds_get(ssid, pass) || strcmp(ssid, "Fishbowl") || strcmp(pass, "hgfedcba"))
                UF("G: a saved name joined with a new password: %d saved, Fishbowl %d times, first '%s'", w->n, fish, ssid);
            net_port_off();
        }
        /* NO STALE ADD: an ordinary check, an add, an ordinary check, in one run - each begins its own way */
        update_begin(50, false);
        if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Fishbowl")) UF("G: a check before an add: '%s'", update_ssid());
        ADV(2); ADV(2); if (update_page() != UPD_PG_OFFER) UF("G: (the check: page %d)", update_page());
        net_port_off();
        update_begin_add(50, false);
        if (update_ssid()[0]) UF("G: an add after a check began CONNECTING to '%s'", update_ssid());
        ADV(1.5f); if (update_page() != UPD_PG_SCAN) UF("G: an add after a check: page %d (want the list)", update_page());
        net_port_off();
        update_begin(50, false);
        if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Fishbowl")) UF("G: a check after an add began at the list (a stale add): '%s'", update_ssid());
        ADV(0.5f); if (update_page() != UPD_PG_BUSY || !strcmp(update_message_title(), "LOOKING FOR NETWORKS")) UF("G: a check after an add is LOOKING FOR NETWORKS");
        net_port_off();
    }
    /* ---- H: the request's words (firmware/main/update_req.h, update_mode.c's) ---- */
    {   uint32_t req = 0x12345678u, chk = 0x9abcdef0u; bool add = true;
        if (update_req_take(&req, &chk, &add) || add || req || chk) UF("H: a cold boot's noise read as a request (add %d)", add);
        update_req_write(&req, &chk, false);
        if (req != UPD_REQ_MAGIC || chk != ~UPD_REQ_MAGIC) UF("H: CHECK FOR UPDATES' words are %08x / %08x (a tank asked by an older build's word must still be understood)", (unsigned)req, (unsigned)chk);
        add = true;
        if (!update_req_take(&req, &chk, &add) || add) UF("H: CHECK FOR UPDATES' request read as %s", add ? "ADD A NETWORK" : "none");
        add = true;
        if (update_req_take(&req, &chk, &add) || add) UF("H: a request was good for a second boot");
        update_req_write(&req, &chk, true);
        if (req == UPD_REQ_MAGIC || !update_req_take(&req, &chk, &add) || !add) UF("H: ADD A NETWORK's request not read as one");
        update_req_write(&req, &chk, false);
        if (!update_req_take(&req, &chk, &add) || add) UF("H: a check asked after an add still reads as ADD A NETWORK (stale)");
        update_req_write(&req, &chk, true); chk ^= 1;
        if (update_req_take(&req, &chk, &add) || add) UF("H: a request whose second word does not match was taken");
        req = UPD_REQ_MAGIC_ADD; chk = ~UPD_REQ_MAGIC;                 /* one request's word, the other's complement */
        if (update_req_take(&req, &chk, &add) || add) UF("H: mixed words were taken");
    }
    /* ---- I: the longest build ids ---- */
    {   static const char *const IDS[] = { "e8d49ea", "e8d49ea-dirty", "123456789-early12", "123456789abc-dirty", "123456789abc-early55",
                                           "0123456789abcdef0123456-early55" };   /* (31: all the app description's version field holds) */
        static const char *const LABEL[2] = { "BUILD", "HATCHERY CREW" };
        for (unsigned k = 0; k < sizeof IDS / sizeof IDS[0]; k++) {
            for (int l = 0; l < 2; l++) {
                char out[40]; const int x = updates_info_fit(LABEL[l], IDS[k], out, sizeof out), w = render_text_w(out, 2);
                const int min_x = UPD_X + render_text_w(LABEL[l], 2) + 14, room = UPD_X + UPD_W - min_x;
                if (x < min_x || x + w > UPD_X + UPD_W) UF("I: %s %s is drawn at page x %d..%d (the label ends at %d, the column at %d)", LABEL[l], IDS[k], x, x + w, min_x - 14, UPD_X + UPD_W);
                if (render_text_w(IDS[k], 2) <= room && strcmp(out, IDS[k])) UF("I: %s %s fits its row and was cut to '%s'", LABEL[l], IDS[k], out);
                if (strcmp(out, IDS[k]) && (strncmp(out, IDS[k], 4) || !strstr(out, "...") || strcmp(out + strlen(out) - 4, IDS[k] + strlen(IDS[k]) - 4))) UF("I: %s cut to '%s' (want its head, ..., its tail)", IDS[k], out);
                if (UPD_VAL_X + render_text_w(IDS[k], 2) <= UPD_X + UPD_W && x != UPD_VAL_X) UF("I: %s fits the values' column and is drawn at %d", IDS[k], x);
            }
            setenv("POCKET_TANK_BUILD_ID", IDS[k], 1);
            updates_page_open(); render_updates_page(fb, TANK_W);
            int bx = 0, by = 0; const char *bad = u_fits(&bx, &by);
            if (bad) UF("I: the UPDATES face with build %s draws %s at frame %d,%d", IDS[k], bad, bx, by);
            for (int y = PGY(UPD_INFO_Y + UPD_INFO_DY); y < PGY(UPD_INFO_Y + UPD_INFO_DY + 14); y++)     /* BUILD's line: ink between the label and the value */
                for (int x = PGX(UPD_X + render_text_w("BUILD", 2)); x < PGX(UPD_X + render_text_w("BUILD", 2) + 12); x++)
                    if (fb[y * TANK_W + x] != fb[0]) UF("I: build %s runs into its label at frame %d,%d", IDS[k], x, y);
        }
        unsetenv("POCKET_TANK_BUILD_ID");
    }
    store_reset(); updates_page_open();
    { char p[700]; snprintf(p, sizeof p, "%s", getenv("POCKET_TANK_WIFI")); remove(p); strncat(p, ".nets", sizeof p - strlen(p) - 1); remove(p); }
    printf("selftest-updui: %d x %d - UPDATES and WI-FI: one column, one button height, each control hit where drawn, both faces inside the glass with 0..3 saved; "
           "the door, BACK and BACK; the door's double never adds or forgets, BACK's double never closes twice, 0.25 s on a tap acts; SURE? ends with its face; "
           "ADD A NETWORK begins at the list, saves beside the others, checks, asks before a fourth, PLUG IN under the gate\n", TANK_W, TANK_H);
    printf("selftest-updui: PASS\n");
    return 0;
}
