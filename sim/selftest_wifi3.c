#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_wifi3.c - --selftest-wifi3 (0.4.2, common/wifi_nets.h): THREE SAVED NETWORKS. Strato: "up to three wifi
 * networks and it tries them one at a time until it finds an SSID match". The list and its rules are pure
 * (common/wifi_nets.c), the pages are common/update.c's, and the store here is the sim's port with the boards' order
 * of writes (net_port_sim.c).
 *   A  the list: one, two, three saved (the newest first); a fourth takes the place of the one used longest ago and
 *      says which; joining one moves it to the front, so the one that goes is another; a saved name saved again takes
 *      the new password and no new place; forget one - the others stay, in their order, and its password is not left
 *      in the free place; names of 32 characters, with spaces and quotes; a name too long for its row
 *   B  the pick: nothing in range -> none; one -> that one; several -> the FIRST in the tank's order (the one used
 *      most recently), whatever order (strength) the radio heard them in; one in range that will not join is passed
 *      over for the next; heard and tried are kept BY NAME, so a list that reorders or loses a network between the
 *      scan and the ask still means the same networks; a name typed on a command line (spaces round it go, only
 *      spaces is no name)
 *   C  the bytes: a list round trips; a short, long, wrong-version, over-full, unterminated, empty-named or
 *      twice-named list reads as nothing; a channel past 14 reads as not known
 *   D  FROM 0.4.1, the rule: its one network (ssid / pass / chan) is added to the list, the list written, then the old
 *      keys erased. A power cut before the write or between the write and the erase loses nothing; a torn list is
 *      made again from the old network; forgotten, it stays forgotten; a save or a forget the cut stopped says so and
 *      changes nothing; a network saved (or re-passworded) under a rolled-back 0.4.1 comes into the list on the way up
 *   E  the store: set adds, a fourth replaces (net_port_creds_replaces said which), forget one, forget all - and a
 *      cut in the middle of forget all leaves the list whole
 *   F  update mode: the one used last is tried first with NO scan (one saved network not in range ends on NO SIGNAL
 *      without one, as 0.4.1); not in range with others saved -> one scan -> the first saved one in range, joined and
 *      first from now on; one in range that will not answer is passed over; one that refuses its password stops
 *      there; none in range -> NO SIGNAL / NO SAVED NETWORK / IS IN RANGE, the list untouched, TRY AGAIN starts over;
 *      a first try that missed the one used last is not held against it (the scan hears it: one more try); the
 *      port's answer after a scan follows the list as it is now, by name
 *   G  the wizard with three saved: a fourth picked from the list says THREE NETWORKS SAVED, the name that goes and
 *      WILL BE FORGOTTEN TO MAKE ROOM before a letter is typed; OTHER NETWORK and BACK TO TANK change nothing; OK,
 *      the password, joined: that one is replaced and no other; a name already saved is not asked about
 *   H  the WI-FI page (the UPDATES page's other face): a FORGET on each row and none beyond the last; one tap arms THAT row (SURE?), a tap on
 *      another row's arms that one instead, a doubled tap is spent, the second tap forgets that name alone; SURE?
 *      moves with its network when the list changes under the page, and goes when that network leaves the list
 *   I  on this board's glass: the page with three networks (a 32-character name among them, one armed), the THREE
 *      NETWORKS SAVED page naming a 32-character network and NO SIGNAL draw nothing off the glass, nothing between
 *      a name and its FORGET, nothing right of the panel
 * Red before 0.4.2: A fails at the second network (one replaced the other). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tank.h"
#include "render.h"
#include "update.h"
#include "wifi_nets.h"

void net_sim_advance(float dt);
void net_sim_see(const char *const *names, int n);
void net_sim_cut_at(int k);
void net_sim_cold(int n);

#define WF(...) do { printf("FAIL: wifi3: "); printf(__VA_ARGS__); printf("\n"); return 1; } while (0)
#define PGX(x) (PAGE_X + (x))
#define PGY(y) (PAGE_Y + (y))
#define ADV(secs) do { net_sim_advance(secs); update_tick(secs); } while (0)
#define TAP(X, Y) (update_touch(PGX(X), PGY(Y), true), update_touch(PGX(X), PGY(Y), false))
#define KEY(cell) TAP(UPD_KB_X + ((cell) % UPD_KB_COLS) * UPD_KB_PX + UPD_KB_W / 2, UPD_KB_Y + ((cell) / UPD_KB_COLS) * UPD_KB_PY + UPD_KB_H / 2)
#define ROWY(r)   (UPD_ROW_Y0 + (r) * UPD_ROW_H + 16)
#define FORGET_X  (UPD_FORGET_X + UPD_FORGET_W / 2)
#define FORGET_Y(r) (UPD_FORGET_Y + (r) * UPD_NET_DY + UPD_FORGET_H / 2)
static const char LONG32[] = "The Longest Network Name Is-32ch";             /* 32 characters */
static const char QUOTED[] = "Strato's \"Guest\" Wi-Fi";
static uint16_t fb[TANK_W * TANK_H];
static char wifi_file[640], nets_file[680];

static int order_is(const wifi_nets_t *w, const char *a, const char *b, const char *c) {
    const char *want[3] = { a, b, c }; int n = 0;
    while (n < 3 && want[n]) n++;
    if (w->n != n) return 0;
    for (int i = 0; i < n; i++) if (strcmp(w->net[i].ssid, want[i])) return 0;
    return 1;
}
static void store_reset(void) { net_sim_cut_at(0); net_sim_cold(0); net_sim_see(NULL, 0); net_port_creds_forget(); remove(wifi_file); remove(nets_file); }
static void legacy_write(const char *ssid, const char *pass, int chan) {
    FILE *f = fopen(wifi_file, "w"); if (!f) return;
    fprintf(f, "%s\n%s\n", ssid, pass); if (chan) fprintf(f, "%d\n", chan);
    fclose(f);
}
static int file_there(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }
static bool on_glass(int x, int y) {
    return y >= 0 && y < TANK_H && x >= tank_glass_x0((float)y) && x <= tank_glass_x1((float)y) && y >= tank_glass_top((float)x);
}
/* the frame just drawn: nothing but the page's ink off the glass, right of the panel, or (rows > 0) between a
   network's name and its FORGET. NULL = fine, else what was wrong */
static const char *frame_fits(int rows, int *bx, int *by) {
    const uint16_t ink = fb[0];                              /* (the frame's corner: the page's ink on every board) */
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
        if (fb[y * TANK_W + x] == ink) continue;
        *bx = x; *by = y;
        if (!on_glass(x, y)) return "off the glass";
        const int px0 = rows < 0 ? 32 : UPD_X, px1 = rows < 0 ? 32 + UPD_PANEL_W : UPD_X + UPD_W;   /* (rows < 0: update mode's panel; else the tank's two faces') */
        if (x >= PGX(px1)) return "right of the panel";
        if (x < PGX(px0)) return "left of the panel";
        /* BETWEEN A NAME AND ITS FORGET: the 8 px before each saved row's FORGET. Until the WI-FI page that strip was bare
           ink and any pixel in it was a name run too far. A row is now a panel, so exactly two things may stand there and
           each only in its own place: the row's outline on the row's first and last line (one pixel each), and the panel's
           fill on the lines between. Both are read from the row's own left end (2 px in, where nothing else is drawn - the
           check mark starts 8 px in), so a letter's white, a caption's teal or any other color in the strip still fails,
           on every line, as before. */
        if (rows > 0 && x >= PGX(UPD_FORGET_X - 8) && x < PGX(UPD_FORGET_X) && y >= PGY(UPD_NET_Y0) && y < PGY(UPD_NET_Y0 + rows * UPD_NET_DY)) {
            const int ry = (y - PGY(UPD_NET_Y0)) % UPD_NET_DY, top = y - ry;
            if (ry >= UPD_NET_H) return "between two rows";          /* (the gap under a row: ink only) */
            const uint16_t want = (ry == 0 || ry == UPD_NET_H - 1) ? fb[top * TANK_W + PGX(UPD_X) + 2] : fb[(top + 2) * TANK_W + PGX(UPD_X) + 2];
            if (fb[y * TANK_W + x] != want) return "between a name and its FORGET";
        }
    }
    return NULL;
}
static void ppm(const char *prefix, const char *name) {
    char path[700]; snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t v = FB_SWAP16(fb[i]);
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
static void paths(const char *tag) {
    const char *sv = getenv("POCKET_TANK_SAVE");
    snprintf(wifi_file, sizeof wifi_file, "%s.%s.wifi", sv && sv[0] ? sv : "/tmp/pt-wifi3", tag);
    snprintf(nets_file, sizeof nets_file, "%s.nets", wifi_file);
    setenv("POCKET_TANK_WIFI", wifi_file, 1);
    unsetenv("POCKET_TANK_FAKE_UPDATE");
}
/* three saved, in the tank's order A (used last), B, C - each joined, the way a keeper's tank gets them */
static void three(const char *a, const char *b, const char *c) {
    const char *n[3] = { c, b, a };
    net_port_creds_forget();
    for (int i = 0; i < 3; i++) if (n[i]) net_port_creds_set(n[i], "password-1");
}
/* the wizard from NO SIGNAL to the list: OTHER NETWORK, the scan */
static void to_list(void) {
    TAP(UPD_BTN_R_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2 - 50);
    ADV(2);
}

int selftest_wifi3(void) {
    paths("selftest");
    store_reset();
    wifi_nets_t w; char gone[NET_SSID_MAX + 1];

    /* ---- A: the list ---- */
    wifi_nets_clear(&w);
    if (!wifi_nets_add(&w, "Home", "pw-home-1", gone) || !order_is(&w, "Home", NULL, NULL) || gone[0]) WF("A: one network saved -> %d", w.n);
    if (!wifi_nets_add(&w, "School", "pw-school", gone) || !order_is(&w, "School", "Home", NULL) || gone[0]) WF("A: a second network took the first one's place or the wrong order (%d saved, first %s)", w.n, w.net[0].ssid);
    if (!wifi_nets_add(&w, "Grandma", "pw-gran-1", gone) || !order_is(&w, "Grandma", "School", "Home") || gone[0]) WF("A: three networks -> %d saved", w.n);
    if (wifi_nets_victim(&w, "Hotspot") != 2 || wifi_nets_victim(&w, "School") != -1) WF("A: what a fourth would replace: %d (want the last), a saved name: %d (want none)", wifi_nets_victim(&w, "Hotspot"), wifi_nets_victim(&w, "School"));
    if (!wifi_nets_add(&w, "Hotspot", "pw-hot-12", gone) || !order_is(&w, "Hotspot", "Grandma", "School") || strcmp(gone, "Home")) WF("A: a fourth network replaced '%s' (want Home, the one used longest ago); first now %s, %d saved", gone, w.net[0].ssid, w.n);
    if (!wifi_nets_joined(&w, "School", 11) || !order_is(&w, "School", "Hotspot", "Grandma") || w.net[0].chan != 11 || !w.net[0].joined) WF("A: joining School did not put it first with its channel");
    if (wifi_nets_joined(&w, "School", 11)) WF("A: joining the first one again, same channel, asked for a write");
    if (!wifi_nets_joined(&w, "School", 6) || w.net[0].chan != 6) WF("A: a moved channel was not learned");
    if (wifi_nets_joined(&w, "School", 0) || w.net[0].chan != 6) WF("A: an unknown channel wiped the learned one");
    if (wifi_nets_joined(&w, "Nobody", 3)) WF("A: joining a network that is not saved changed the list");
    if (!wifi_nets_add(&w, "Cabin", "pw-cabin-1", gone) || strcmp(gone, "Grandma") || !order_is(&w, "Cabin", "School", "Hotspot")) WF("A: after School was joined, a new one replaced '%s' (want Grandma)", gone);
    /* a saved name again: the new password, no new place, what it learned kept */
    if (!wifi_nets_add(&w, "School", "pw-school-NEW", gone) || gone[0] || !order_is(&w, "School", "Cabin", "Hotspot") || strcmp(w.net[0].pass, "pw-school-NEW") || w.net[0].chan != 6 || !w.net[0].joined)
        WF("A: School saved again: %d saved, replaced '%s', password %s, channel %d", w.n, gone, strcmp(w.net[0].pass, "pw-school-NEW") ? "OLD" : "new", w.net[0].chan);
    if (strcmp(w.net[1].pass, "pw-cabin-1") || strcmp(w.net[2].pass, "pw-hot-12")) WF("A: another network's password changed");
    if (wifi_nets_add(&w, "", "x", gone) || w.n != 3) WF("A: a network with no name was saved");
    /* forget one */
    if (!wifi_nets_forget_one(&w, "Cabin") || !order_is(&w, "School", "Hotspot", NULL)) WF("A: forget the middle one: %d left, first %s", w.n, w.net[0].ssid);
    { const unsigned char *z = (const unsigned char *)&w.net[2]; for (size_t i = 0; i < sizeof w.net[2]; i++) if (z[i]) WF("A: a forgotten network's bytes stay in the free place (byte %d)", (int)i); }
    if (wifi_nets_forget_one(&w, "Cabin") || w.n != 2) WF("A: forgetting one that is not saved did something");
    if (!wifi_nets_forget_one(&w, "School") || !order_is(&w, "Hotspot", NULL, NULL) || !wifi_nets_forget_one(&w, "Hotspot") || w.n) WF("A: forgetting the rest left %d", w.n);
    /* long names, odd names */
    wifi_nets_clear(&w);
    if (strlen(LONG32) != 32) WF("A: the test's long name is %d characters", (int)strlen(LONG32));
    wifi_nets_add(&w, LONG32, "p", gone); wifi_nets_add(&w, QUOTED, "it's \"a\" pass word", gone); wifi_nets_add(&w, " lead and trail ", "", gone);
    if (wifi_nets_find(&w, LONG32) != 2 || strcmp(w.net[2].ssid, LONG32)) WF("A: a 32-character name was not kept whole");
    if (wifi_nets_find(&w, QUOTED) != 1 || strcmp(w.net[1].pass, "it's \"a\" pass word")) WF("A: a name with spaces and quotes, or its password, was not kept as typed");
    if (wifi_nets_find(&w, " lead and trail ") != 0 || wifi_nets_find(&w, "lead and trail") != -1) WF("A: a name's own spaces were trimmed");
    if (wifi_nets_find(&w, "the longest network name is-32ch") != -1) WF("A: names matched without regard to case");
    { char p64[80]; memset(p64, 'k', 70); p64[70] = 0; wifi_nets_add(&w, QUOTED, p64, gone);
      if ((int)strlen(w.net[0].pass) != NET_PASS_MAX) WF("A: a password past %d characters was kept as %d", NET_PASS_MAX, (int)strlen(w.net[0].pass)); }
    #define FIT22 22   /* (the rule, at the width the page had when it was written; the boards' rows hold UPD_NAME_CHARS: 21, the bowl 19) */
    { char s[FIT22 + 1], t[FIT22 + 1];
      wifi_name_fit(s, sizeof s, "Fishbowl", FIT22); if (strcmp(s, "Fishbowl")) WF("A: a short name shown as '%s'", s);
      wifi_name_fit(s, sizeof s, "Exactly-22-characters", FIT22 + 0); if (strlen("Exactly-22-characters.") != FIT22) WF("A: (the test's 22)");
      wifi_name_fit(s, sizeof s, "Exactly-22-characters.", FIT22); if (strcmp(s, "Exactly-22-characters.")) WF("A: a name that fits was cut: '%s'", s);
      wifi_name_fit(s, sizeof s, LONG32, FIT22);
      if ((int)strlen(s) != FIT22 || strncmp(s, LONG32, 12) || !strstr(s, "...") || strcmp(s + FIT22 - 7, LONG32 + 32 - 7)) WF("A: the 32-character name shown as '%s' (want %d characters: its head, ..., its tail)", s, FIT22);
      wifi_name_fit(s, sizeof s, "Family Network Upstairs-2G", FIT22); wifi_name_fit(t, sizeof t, "Family Network Upstairs-5G", FIT22);
      if (!strcmp(s, t)) WF("A: two long names that differ at the end are shown the same ('%s')", s);
      { char u[UPD_NAME_CHARS + 1]; wifi_name_fit(u, sizeof u, LONG32, UPD_NAME_CHARS); snprintf(s, sizeof s, "%s", u); }   /* (this board's row) */
      if (update_text_w(s, 2) > UPD_FORGET_X - 8 - UPD_NAME_X) WF("A: a cut name is %d px wide, its room is %d", update_text_w(s, 2), UPD_FORGET_X - 8 - UPD_NAME_X); }

    /* ---- B: the pick ---- */
    wifi_nets_clear(&w); wifi_nets_add(&w, "Grandma", "g", gone); wifi_nets_add(&w, "School", "s", gone); wifi_nets_add(&w, "Home", "h", gone);   /* Home, School, Grandma */
    { wifi_names_t seen, tried; wifi_names_clear(&seen); wifi_names_clear(&tried);
      #define HEAR(...) do { const char *h_[] = { __VA_ARGS__ }; wifi_names_clear(&seen); for (size_t k_ = 0; k_ < sizeof h_ / sizeof h_[0]; k_++) wifi_nets_heard(&w, &seen, h_[k_]); } while (0)
      HEAR("Neighbor 5G", "xfinitywifi");
      if (seen.n || wifi_nets_next(&w, &seen, &tried) != -1 || wifi_nets_left(&w, &seen, &tried)) WF("B: nothing saved in range, and one was picked");
      HEAR("xfinitywifi", "Grandma", "Grandma");
      if (seen.n != 1 || wifi_nets_next(&w, &seen, &tried) != 2) WF("B: only Grandma in range (heard twice) -> %d of %d", wifi_nets_next(&w, &seen, &tried), seen.n);
      HEAR("Grandma", "Neighbor 5G", "School");
      if (wifi_nets_next(&w, &seen, &tried) != 1) WF("B: Grandma (heard first, the strongest) and School in range -> %d (want School: used more recently)", wifi_nets_next(&w, &seen, &tried));
      if (wifi_nets_next(&w, &seen, NULL) != 1) WF("B: no tried list given");
      HEAR("Grandma", "School", "Home");
      if (wifi_nets_next(&w, &seen, &tried) != 0 || wifi_nets_left(&w, &seen, &tried) != 3) WF("B: all three in range -> %d (want Home, the one used last)", wifi_nets_next(&w, &seen, &tried));
      /* ONE IN RANGE THAT WILL NOT JOIN IS PASSED OVER (review 1): Home absent; School's name is there but refuses; Grandma is good */
      HEAR("School", "Grandma");
      if (wifi_nets_next(&w, &seen, &tried) != 1 || wifi_nets_left(&w, &seen, &tried) != 2) WF("B: School and Grandma in range: next %d, left %d", wifi_nets_next(&w, &seen, &tried), wifi_nets_left(&w, &seen, &tried));
      wifi_names_add(&tried, "School");
      if (wifi_nets_next(&w, &seen, &tried) != 2 || wifi_nets_left(&w, &seen, &tried) != 1) WF("B: School tried and refused: next is %d (want Grandma - she was never tried), %d left", wifi_nets_next(&w, &seen, &tried), wifi_nets_left(&w, &seen, &tried));
      wifi_names_add(&tried, "Grandma"); wifi_names_add(&tried, "Grandma");
      if (tried.n != 2 || wifi_nets_next(&w, &seen, &tried) != -1 || wifi_nets_left(&w, &seen, &tried)) WF("B: both tried: one is still offered");
      /* BY NAME, NOT BY PLACE (review 4): the list reorders when one joins; what was heard and tried still means the same networks */
      wifi_names_clear(&tried); wifi_names_add(&tried, "School");
      wifi_nets_joined(&w, "Grandma", 1);                    /* Grandma, Home, School */
      if (wifi_nets_next(&w, &seen, &tried) != 0 || strcmp(w.net[0].ssid, "Grandma")) WF("B: after Grandma joined (the list reordered), next is %s (want Grandma: heard, not tried)", w.net[wifi_nets_next(&w, &seen, &tried) < 0 ? 0 : wifi_nets_next(&w, &seen, &tried)].ssid);
      wifi_names_add(&tried, "Grandma");
      if (wifi_nets_next(&w, &seen, &tried) != -1) WF("B: after a reorder a network not in range (Home) or one already tried is offered (%d)", wifi_nets_next(&w, &seen, &tried));
      wifi_nets_forget_one(&w, "Grandma"); wifi_names_clear(&tried);   /* Home, School: Grandma was heard but is not saved any more */
      if (wifi_nets_next(&w, &seen, &tried) != 1) WF("B: a forgotten network that was heard is offered, or School is not (%d)", wifi_nets_next(&w, &seen, &tried));
      { wifi_names_t four; wifi_names_clear(&four); wifi_names_add(&four, "a"); wifi_names_add(&four, "b"); wifi_names_add(&four, "c"); wifi_names_add(&four, "d"); wifi_names_add(&four, "");
        if (four.n != 3 || wifi_names_has(&four, "d") || !wifi_names_has(&four, "c")) WF("B: a fourth name was kept"); }
      #undef HEAR
    }
    /* a name from a command line (review 5): the spaces round it go, the ones inside stay */
    { char nm[NET_SSID_MAX + 1];
      wifi_name_arg(nm, "  Home  ", 8); if (strcmp(nm, "Home")) WF("B: ' Home ' typed -> '%s'", nm);
      wifi_name_arg(nm, " My Home Wi-Fi pass=x", 14); if (strcmp(nm, "My Home Wi-Fi")) WF("B: a name with spaces inside -> '%s'", nm);
      wifi_name_arg(nm, "    ", 4); if (nm[0]) WF("B: only spaces typed -> '%s' (want no name: it must never mean ALL)", nm);
      wifi_name_arg(nm, "", 0); if (nm[0]) WF("B: nothing typed -> '%s'", nm);
      wifi_name_arg(nm, "0123456789012345678901234567890123456789", 40); if (strlen(nm) != NET_SSID_MAX) WF("B: a 40-character name kept as %d", (int)strlen(nm)); }

    /* ---- C: the bytes ---- */
    { wifi_nets_t a, b; unsigned char raw[sizeof a + 4];
      wifi_nets_clear(&a); wifi_nets_add(&a, LONG32, "pw one", gone); wifi_nets_add(&a, QUOTED, "", gone); wifi_nets_joined(&a, LONG32, 13);
      memcpy(raw, &a, sizeof a);
      if (!wifi_nets_from_bytes(&b, raw, sizeof a) || memcmp(&a, &b, sizeof a)) WF("C: a list did not read back as written");
      if (!wifi_nets_from_bytes(&a, &a, sizeof a) || memcmp(&a, &b, sizeof a)) WF("C: a list read in place changed");
      if (wifi_nets_from_bytes(&b, raw, sizeof a - 1) || b.n) WF("C: a short list was taken");
      if (wifi_nets_from_bytes(&b, raw, sizeof a + 1) || b.n) WF("C: a long list was taken");
      if (wifi_nets_from_bytes(&b, NULL, 0) || b.n) WF("C: no bytes were taken");
      raw[0] = 2; if (wifi_nets_from_bytes(&b, raw, sizeof a) || b.n) WF("C: another version's list was taken"); raw[0] = WIFI_NETS_VER;
      raw[1] = 4; if (wifi_nets_from_bytes(&b, raw, sizeof a) || b.n) WF("C: a list of four was taken"); raw[1] = 2;
      memcpy(raw, &a, sizeof a); memset(raw + 2, 'x', NET_SSID_MAX + 1); if (wifi_nets_from_bytes(&b, raw, sizeof a) || b.n) WF("C: a name with no end was taken");
      memcpy(raw, &a, sizeof a); raw[2] = 0; if (wifi_nets_from_bytes(&b, raw, sizeof a) || b.n) WF("C: an empty name was taken");
      memcpy(raw, &a, sizeof a); memcpy(raw + 2 + sizeof(wifi_net_t), raw + 2, sizeof(wifi_net_t)); if (wifi_nets_from_bytes(&b, raw, sizeof a) || b.n) WF("C: the same name twice was taken");
      memcpy(raw, &a, sizeof a); raw[2 + NET_SSID_MAX + 1 + NET_PASS_MAX + 1] = 99; raw[2 + NET_SSID_MAX + 1 + NET_PASS_MAX + 2] = 7;   /* a channel that is none, an odd "joined" (review 9) */
      if (!wifi_nets_from_bytes(&b, raw, sizeof a) || b.net[0].chan != 0 || b.net[0].joined != 1 || strcmp(b.net[0].ssid, a.net[0].ssid)) WF("C: channel 99 read back as %d (want 0, not known), joined as %d", b.net[0].chan, b.net[0].joined);
      raw[2 + NET_SSID_MAX + 1 + NET_PASS_MAX + 1] = 14; if (!wifi_nets_from_bytes(&b, raw, sizeof a) || b.net[0].chan != 14) WF("C: channel 14 not kept");
      memcpy(raw, &a, sizeof a); raw[1] = 1; raw[2 + sizeof(wifi_net_t) + 5] = 'Z';      /* a count of one: what rides behind it is not kept */
      if (!wifi_nets_from_bytes(&b, raw, sizeof a) || b.n != 1 || b.net[1].ssid[0] || b.net[1].ssid[5]) WF("C: bytes behind the last network were kept"); }

    /* ---- D: from 0.4.1 (the rule: found -> added to the list -> the list written -> the old keys erased) ---- */
    { wifi_nets_clear(&w);
      if (!wifi_nets_take_old(&w, "Old Home", "old-pass-1", 6) || !order_is(&w, "Old Home", NULL, NULL) || strcmp(w.net[0].pass, "old-pass-1") || w.net[0].chan != 6 || !w.net[0].joined) WF("D: 0.4.1's network with a learned channel -> %d saved, channel %d, joined %d", w.n, w.net[0].chan, w.net[0].joined);
      wifi_nets_take_old(&w, "Old Home", "old-pass-1", 6);
      if (!order_is(&w, "Old Home", NULL, NULL)) WF("D: the same old network taken twice (a cut between the write and the erase) -> %d saved", w.n);
      wifi_nets_clear(&w); wifi_nets_take_old(&w, "Old Home", "", 0);
      if (w.n != 1 || w.net[0].chan || w.net[0].joined || w.net[0].pass[0]) WF("D: 0.4.1's open network never joined -> channel %d, joined %d", w.net[0].chan, w.net[0].joined);
      wifi_nets_clear(&w); if (wifi_nets_take_old(&w, "", "x", 6) || w.n) WF("D: no network in 0.4.1 became one"); }
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1]; bool joined = false;
    store_reset(); legacy_write(QUOTED, "old pass 99", 11);
    net_sim_cut_at(1);                                       /* the power goes as the list is about to be written */
    if (!net_port_creds_get(ssid, pass) || strcmp(ssid, QUOTED) || strcmp(pass, "old pass 99")) WF("D: 0.4.1's network was not read (a cut before the list was written)");
    if (file_there(nets_file) || !file_there(wifi_file)) WF("D: cut before the write: the list is %s, 0.4.1's network %s", file_there(nets_file) ? "there" : "not there", file_there(wifi_file) ? "there" : "GONE");
    net_sim_cut_at(2);                                       /* the next boot: the list is written, the power goes before the old keys are erased */
    if (net_port_creds_count() != 1) WF("D: the list was not made (cut after its write)");
    if (!file_there(nets_file) || !file_there(wifi_file)) WF("D: cut between the write and the erase: the list %s, 0.4.1's network %s (want both)", file_there(nets_file) ? "there" : "NOT there", file_there(wifi_file) ? "there" : "gone");
    net_sim_cut_at(0);
    if (net_port_creds_count() != 1 || !net_port_creds_at(0, ssid, &joined) || strcmp(ssid, QUOTED) || !joined || !net_port_creds_get(ssid, pass) || strcmp(pass, "old pass 99")) WF("D: the boot after the cuts: 0.4.1's network is not the one network of the list (%d saved)", net_port_creds_count());
    if (file_there(wifi_file)) WF("D: 0.4.1's keys were not erased once the list was written (the two could disagree later)");
    if (net_port_nets()->net[0].chan != 11) WF("D: 0.4.1's learned channel was lost (%d)", net_port_nets()->net[0].chan);
    { FILE *f = fopen(nets_file, "wb"); if (f) { fwrite("\1\1torn", 1, 6, f); fclose(f); } }   /* a list that is not whole, the old keys still there */
    legacy_write(QUOTED, "old pass 99", 11);
    if (net_port_creds_count() != 1 || !net_port_creds_get(ssid, pass) || strcmp(ssid, QUOTED)) WF("D: a torn list beside 0.4.1's network: the network was lost");
    /* FORGOTTEN STAYS FORGOTTEN: the old keys are gone before a FORGET can be tapped, and a FORGET cut at any step brings nothing back */
    if (!net_port_creds_forget_one(QUOTED) || net_port_creds_count() != 0 || net_port_creds_get(ssid, pass)) WF("D: 0.4.1's network, forgotten, is still there");
    if (file_there(wifi_file) || net_port_creds_count() != 0) WF("D: a forgotten network can come back from 0.4.1's keys");
    store_reset(); legacy_write("Old Home", "old-pass-1", 0);
    net_sim_cut_at(2); net_port_creds_count();               /* (the move cut before the erase: the list and the old keys both hold it) */
    net_sim_cut_at(0);
    if (!net_port_creds_forget_one("Old Home") || net_port_creds_count() != 0 || file_there(wifi_file)) WF("D: forgotten after a cut move: %d saved, 0.4.1's keys %s", net_port_creds_count(), file_there(wifi_file) ? "STILL THERE" : "gone");
    net_port_creds_set("A", "a-pass-11"); net_port_creds_set("B", "b-pass-11");
    net_sim_cut_at(1);                                       /* a FORGET that never reached the flash: nothing changed, and it said so */
    if (net_port_creds_forget_one("A") || (net_sim_cut_at(0), net_port_creds_count()) != 2) WF("D: a forget the power cut: said it worked, or %d saved", net_port_creds_count());
    net_sim_cut_at(1);
    if (net_port_creds_set("Third", "third-pw-1")) WF("D: a save the power cut said it worked");
    net_sim_cut_at(0);
    if (net_port_creds_count() != 2) WF("D: a save cut before its write changed the list (%d)", net_port_creds_count());
    /* DOWN TO 0.4.1 AND UP AGAIN (review 2). 0.4.1 sees no network (its keys are gone). What it is given there comes into
       the list on the way up: a new name first, a name already in the list with 0.4.1's (newer) password */
    legacy_write("Cabin", "cabin-pass", 3);
    if (net_port_creds_count() != 3 || !order_is(net_port_nets(), "Cabin", "B", "A") || !net_port_nets()->net[0].joined || file_there(wifi_file)) WF("D: a network saved under 0.4.1 after a rollback: %d saved, first %s", net_port_creds_count(), net_port_nets()->net[0].ssid);
    legacy_write("A", "a-NEW-pass", 0);
    if (net_port_creds_count() != 3 || !order_is(net_port_nets(), "A", "Cabin", "B") || strcmp(net_port_nets()->net[0].pass, "a-NEW-pass") || file_there(wifi_file)) WF("D: a password changed under 0.4.1 was ignored on the way back up (%s)", net_port_nets()->net[0].ssid);
    net_port_creds_forget_one("B");
    if (!order_is(net_port_nets(), "A", "Cabin", NULL) || file_there(wifi_file)) WF("D: (forget B)");

    /* ---- E: the store ---- */
    store_reset();
    if (net_port_creds_count() || net_port_creds_get(ssid, pass) || net_port_creds_at(0, ssid, NULL)) WF("E: an empty store has a network");
    net_port_creds_set("Home", "home-pass"); net_port_creds_set(QUOTED, "school pw"); net_port_creds_set(LONG32, "");
    if (net_port_creds_count() != 3 || !net_port_creds_get(ssid, pass) || strcmp(ssid, LONG32) || pass[0]) WF("E: three saved -> %d, first %s", net_port_creds_count(), ssid);
    if (!net_port_creds_at(1, ssid, &joined) || strcmp(ssid, QUOTED) || joined || !net_port_creds_at(2, ssid, NULL) || strcmp(ssid, "Home") || net_port_creds_at(3, ssid, NULL)) WF("E: the order of three");
    if (!net_port_creds_replaces("Hotspot", ssid) || strcmp(ssid, "Home") || net_port_creds_replaces(QUOTED, ssid) || ssid[0]) WF("E: what a fourth replaces: '%s'", ssid);
    net_port_creds_set(QUOTED, "school pw 2");               /* a saved name again */
    if (net_port_creds_count() != 3 || !net_port_creds_get(ssid, pass) || strcmp(ssid, QUOTED) || strcmp(pass, "school pw 2")) WF("E: a saved name saved again: %d saved, first %s", net_port_creds_count(), ssid);
    net_port_creds_set("Hotspot", "hot-pass-1");
    if (net_port_creds_count() != 3 || !net_port_creds_at(0, ssid, NULL) || strcmp(ssid, "Hotspot")) WF("E: a fourth is not first");
    if (wifi_nets_find(net_port_nets(), "Home") >= 0 || wifi_nets_find(net_port_nets(), LONG32) != 2 || wifi_nets_find(net_port_nets(), QUOTED) != 1) WF("E: a fourth did not replace Home alone");
    if (!net_port_creds_forget_one(QUOTED) || net_port_creds_count() != 2 || net_port_creds_forget_one(QUOTED)) WF("E: forget one");
    if (!net_port_creds_at(0, ssid, NULL) || strcmp(ssid, "Hotspot") || !net_port_creds_at(1, ssid, NULL) || strcmp(ssid, LONG32)) WF("E: forget one: the others' order");
    net_sim_cut_at(2);                                       /* forget all, cut in the middle */
    net_port_creds_forget();
    net_sim_cut_at(0);
    if (net_port_creds_count() != 2) WF("E: forget all cut halfway left %d of 2 (want the list whole)", net_port_creds_count());
    net_port_creds_forget();
    if (net_port_creds_count() || file_there(nets_file) || file_there(wifi_file)) WF("E: forget all left something");
    legacy_write("Old Home", "old-pass-1", 0); net_port_creds_count();                 /* 0.4.1's keys and the list side by side: all means both */
    net_port_creds_forget();
    if (net_port_creds_count() || net_port_creds_get(ssid, pass)) WF("E: forget all on a tank from 0.4.1 left its network");

    /* ---- F: update mode ---- */
    #define SEE(...) do { static const char *const see_[] = { __VA_ARGS__ }; net_sim_see(see_, (int)(sizeof see_ / sizeof see_[0])); } while (0)
    store_reset(); net_port_creds_set("Home", "home-pass");
    SEE("Neighbor 5G");                                      /* ONE saved, not in range: NO SIGNAL as 0.4.1, and no scan on the way */
    update_begin(50, false);
    if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Home") || net_port_connect_state() != NET_BUSY) WF("F: one saved: no connect at once");
    for (int i = 0; i < 20 && update_page() == UPD_PG_BUSY; i++) { ADV(0.1f); if (net_port_scan_state() == NET_BUSY) WF("F: ONE saved network out of range, and the tank scanned (slower than 0.4.1)"); }
    if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_title(), "NO SIGNAL") || strcmp(update_message_line1(), "Home")) WF("F: one saved, out of range: %s / %s", update_message_title(), update_message_line1());
    three("Home", "School", "Grandma");
    SEE("Grandma", "School", "Home", "Neighbor 5G");         /* the one used last is in range: straight to it, no scan */
    update_begin(50, false);
    if (strcmp(update_ssid(), "Home") || net_port_connect_state() != NET_BUSY) WF("F: three saved: the first try is %s (want Home, used last)", update_ssid());
    for (int i = 0; i < 20 && net_port_connect_state() == NET_BUSY; i++) { ADV(0.1f); if (net_port_scan_state() == NET_BUSY) WF("F: the one used last is in range, and the tank scanned first"); }
    ADV(2);
    if (update_page() != UPD_PG_OFFER || !order_is(net_port_nets(), "Home", "School", "Grandma")) WF("F: Home in range: page %d, first %s", update_page(), net_port_nets()->net[0].ssid);
    SEE("Grandma", "Neighbor 5G", "School");                 /* Home not in range; Grandma is the stronger, School the more recent */
    update_begin(50, false); ADV(2);
    if (update_page() != UPD_PG_BUSY || net_port_scan_state() != NET_BUSY || strcmp(update_message_title(), "LOOKING FOR NETWORKS")) WF("F: Home out of range with others saved: no look for them (page %d, %s)", update_page(), update_message_title());
    ADV(1.5f);
    if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "School") || net_port_connect_state() != NET_BUSY) WF("F: Grandma and School in range -> connecting to %s (want School)", update_ssid());
    ADV(2); ADV(2);
    if (update_page() != UPD_PG_OFFER || !order_is(net_port_nets(), "School", "Home", "Grandma") || !net_port_nets()->net[0].joined) WF("F: School joined: page %d, first %s", update_page(), net_port_nets()->net[0].ssid);
    if (!net_port_creds_get(ssid, pass) || strcmp(pass, "password-1")) WF("F: joining changed a password");
    three("Home", "Ghost", "Grandma");                       /* Ghost is in range and never answers: passed over for Grandma */
    SEE("Ghost", "Grandma");
    update_begin(50, false); ADV(2); ADV(1.5f);
    if (strcmp(update_ssid(), "Ghost")) WF("F: Ghost and Grandma in range -> %s first (want Ghost, used more recently)", update_ssid());
    ADV(2);
    if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Grandma") || net_port_scan_state() == NET_BUSY) WF("F: Ghost did not answer: on to %s (page %d; want Grandma, with no second scan)", update_ssid(), update_page());
    ADV(2); ADV(2);
    if (update_page() != UPD_PG_OFFER || !order_is(net_port_nets(), "Grandma", "Home", "Ghost")) WF("F: Grandma joined after Ghost: first %s", net_port_nets()->net[0].ssid);
    three("Home", "School", "Grandma");                      /* School refuses its password: it stops there */
    net_port_creds_set("School", "wrong-pass"); net_port_creds_set("Home", "password-1");
    if (!order_is(net_port_nets(), "Home", "School", "Grandma")) WF("F: (the test's order)");
    SEE("School", "Grandma");
    update_begin(50, false); ADV(2); ADV(1.5f); ADV(2);
    if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_title(), "NOT ACCEPTED") || strcmp(update_message_line1(), "School")) WF("F: School refused its password: %s / %s", update_message_title(), update_message_line1());
    if (!order_is(net_port_nets(), "Home", "School", "Grandma")) WF("F: a refused password changed the list");
    three("Home", "School", "Grandma");                      /* none in range */
    SEE("Neighbor 5G", "xfinitywifi");
    update_begin(50, false); ADV(2); ADV(1.5f);
    if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_title(), "NO SIGNAL") || strcmp(update_message_line1(), "NO SAVED NETWORK")) WF("F: none of three in range: %s / %s", update_message_title(), update_message_line1());
    if (!order_is(net_port_nets(), "Home", "School", "Grandma")) WF("F: none in range changed the list");
    render_update(fb, TANK_W, 0.1f);
    { int bx = 0, by = 0; const char *bad = frame_fits(-1, &bx, &by); if (bad) WF("I: NO SIGNAL draws %s at frame %d,%d", bad, bx, by); }
    TAP(UPD_BTN_L_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2 - 50);        /* TRY AGAIN: from the one used last, the scan again if need be */
    if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Home")) WF("F: TRY AGAIN starts with %s (want Home)", update_ssid());
    SEE("Grandma");
    ADV(2); ADV(1.5f); ADV(2); ADV(2);
    if (update_page() != UPD_PG_OFFER || !order_is(net_port_nets(), "Grandma", "Home", "School")) WF("F: TRY AGAIN with Grandma now in range: page %d, first %s", update_page(), net_port_nets()->net[0].ssid);

    /* THE FIRST SWEEP MISSED IT (review 7): Home's first try finds nobody, the scan then hears Home and only Home - it is
       tried once more, not told NO SAVED NETWORK IS IN RANGE; missed again, that is the end of it */
    three("Home", "School", "Grandma");
    SEE("Home", "Neighbor 5G");
    net_sim_cold(1);
    update_begin(50, false); ADV(2);
    if (net_port_scan_state() != NET_BUSY) WF("F: (no scan after the cold try)");
    ADV(1.5f);
    if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Home") || net_port_connect_state() != NET_BUSY) WF("F: the first try missed Home and the scan heard it: page %d, %s / %s (want Home tried once more)", update_page(), update_message_title(), update_message_line1());
    ADV(2); ADV(2);
    if (update_page() != UPD_PG_OFFER || !order_is(net_port_nets(), "Home", "School", "Grandma")) WF("F: Home joined at its second try: page %d", update_page());
    net_sim_cold(2);
    update_begin(50, false); ADV(2); ADV(1.5f); ADV(2);
    if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_line1(), "NO SAVED NETWORK") || net_port_scan_state() == NET_BUSY) WF("F: Home heard but silent twice: page %d, %s (want NO SIGNAL after ONE more try, no second scan)", update_page(), update_message_line1());
    net_sim_cold(0);
    /* WHAT THE SCAN HEARD IS KEPT BY NAME (review 4): the list reorders (Grandma is saved again and goes first) between
       the scan and the ask - the answer is still about School and Grandma, not about whoever stands in their old places */
    three("Home", "School", "Grandma");
    SEE("School", "Grandma");
    net_port_scan_start(); net_sim_advance(2);
    { wifi_names_t tried; wifi_names_clear(&tried); wifi_names_add(&tried, "School");
      net_port_creds_set("Grandma", "password-1");
      if (!order_is(net_port_nets(), "Grandma", "Home", "School")) WF("F: (the test's reorder)");
      int i = net_port_scan_saved(&tried, ssid, pass);
      if (i != 0 || strcmp(ssid, "Grandma")) WF("F: the list reordered after the scan: the next to join is %s (place %d; want Grandma - heard, not tried; Home was never heard)", i < 0 ? "none" : ssid, i);
      wifi_names_add(&tried, "Grandma");
      if (net_port_scan_saved(&tried, ssid, pass) != -1) WF("F: School and Grandma both tried, and %s is offered", ssid);
      wifi_names_clear(&tried); net_port_creds_forget_one("Grandma");
      if (net_port_scan_saved(&tried, ssid, pass) != 1 || strcmp(ssid, "School")) WF("F: Grandma forgotten after the scan: next is %s (want School)", ssid); }
    net_port_off();

    /* ---- G: the wizard with three saved ---- */
    three("Home", "School", LONG32);
    SEE("Cafe", "Library");
    update_begin(50, false); ADV(2); ADV(1.5f);
    if (update_page() != UPD_PG_MESSAGE) WF("G: no NO SIGNAL to start from");
    to_list();
    if (update_page() != UPD_PG_SCAN) WF("G: OTHER NETWORK: no list (page %d)", update_page());
    TAP(PAGE_W / 4, ROWY(0));                                 /* Cafe: a fourth */
    if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_title(), "THREE NETWORKS SAVED") || strcmp(update_message_line1(), LONG32))
        WF("G: a fourth network picked with three saved: page %d, '%s' / '%s' (want THREE NETWORKS SAVED and the one used longest ago)", update_page(), update_message_title(), update_message_line1());
    render_update(fb, TANK_W, 0.1f);
    { int bx = 0, by = 0; const char *bad = frame_fits(-1, &bx, &by); if (bad) WF("I: THREE NETWORKS SAVED with a 32-character name draws %s at frame %d,%d", bad, bx, by); }
    if (!order_is(net_port_nets(), "Home", "School", LONG32)) WF("G: the notice itself changed the list");
    TAP(UPD_BTN_R_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2 - 50);        /* OTHER NETWORK: the list again, nothing changed */
    if (update_page() != UPD_PG_SCAN || !order_is(net_port_nets(), "Home", "School", LONG32)) WF("G: OTHER NETWORK on the notice: page %d", update_page());
    TAP(PAGE_W / 4, ROWY(1));                                 /* Library: asked again */
    if (update_page() != UPD_PG_MESSAGE || strcmp(update_message_title(), "THREE NETWORKS SAVED")) WF("G: a second pick was not asked about");
    TAP(UPD_BTN_MID_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2);           /* BACK TO TANK: all three kept */
    if (update_outcome() != UPD_BACK || !order_is(net_port_nets(), "Home", "School", LONG32)) WF("G: BACK TO TANK on the notice: outcome %d, %d saved", update_outcome(), net_port_creds_count());
    update_begin(50, false); ADV(2); ADV(1.5f); to_list();
    TAP(PAGE_W / 4, ROWY(0));
    TAP(UPD_BTN_L_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2 - 50);        /* OK */
    if (update_page() != UPD_PG_PASSWORD || update_typed()[0]) WF("G: OK: no empty keyboard (page %d)", update_page());
    if (!order_is(net_port_nets(), "Home", "School", LONG32)) WF("G: OK replaced the network before the new one had joined");
    for (int i = 0; i < 8; i++) KEY(i);                       /* "abcdefgh" */
    KEY(20);                                                  /* JOIN */
    if (update_page() != UPD_PG_BUSY || strcmp(update_ssid(), "Cafe")) WF("G: JOIN: page %d, %s", update_page(), update_ssid());
    ADV(2);
    if (!order_is(net_port_nets(), "Cafe", "Home", "School") || !net_port_nets()->net[0].joined || strcmp(net_port_nets()->net[0].pass, "abcdefgh")) WF("G: Cafe joined: %d saved, first %s (want Cafe, Home, School: the long name replaced)", net_port_creds_count(), net_port_nets()->net[0].ssid);
    SEE("School", "Library");                                 /* a name already saved, picked from the list: not asked about */
    three("Home", "School", "Grandma"); net_port_creds_set("School", "wrong-old-pass"); net_port_creds_set("Home", "password-1");
    update_begin(50, false); ADV(2); ADV(1.5f); ADV(2);
    if (strcmp(update_message_title(), "NOT ACCEPTED")) WF("G: (School's old password was not refused: %s)", update_message_title());
    TAP(UPD_BTN_L_X + UPD_BTN_W / 2, UPD_BTN_Y + UPD_BTN_H / 2);             /* OTHER NETWORK (row B, left) */
    ADV(2);
    TAP(PAGE_W / 4, ROWY(0));                                 /* School */
    if (update_page() != UPD_PG_PASSWORD) WF("G: a saved network picked from the list was asked about (page %d, %s)", update_page(), update_message_title());
    for (int i = 0; i < 14; i++) KEY(15);                     /* DEL: the refused text is kept for a fix (0.3.0) - here, all of it out */
    if (update_typed()[0]) WF("G: (the keyboard was not emptied: '%s')", update_typed());
    for (int i = 0; i < 8; i++) KEY(i);
    KEY(20); ADV(2);
    if (!order_is(net_port_nets(), "School", "Home", "Grandma") || strcmp(net_port_nets()->net[0].pass, "abcdefgh")) WF("G: School's new password: %d saved, first %s", net_port_creds_count(), net_port_nets()->net[0].ssid);
    net_port_off(); net_sim_see(NULL, 0);

    /* ---- H: the WI-FI page (0.4.2: the UPDATES page's other face - update.h) ---- */
    #define NETS_OPEN() do { updates_page_open(); updates_page_show_nets(true); } while (0)
    #define UPT(r, d_us, u_us) (updates_page_touch(PGX(FORGET_X), PGY(FORGET_Y(r)), true, d_us), updates_page_touch(PGX(FORGET_X), PGY(FORGET_Y(r)), false, u_us))
    store_reset(); NETS_OPEN();
    for (int r = 0; r < 3; r++) if (updates_page_tap(PGX(FORGET_X), PGY(FORGET_Y(r))) != UPD_TAP_NONE) WF("H: a FORGET answers with nothing saved (row %d)", r);
    net_port_creds_set("Home", "home-pass");
    if (updates_page_tap(PGX(FORGET_X), PGY(FORGET_Y(0))) != UPD_TAP_FORGET || updates_page_tap(PGX(FORGET_X), PGY(FORGET_Y(1))) != UPD_TAP_NONE) WF("H: one saved: row 1's FORGET answers, or row 2's does");
    three("Home", QUOTED, LONG32);
    for (int r = 0; r < 3; r++) {
        if (updates_page_tap(PGX(FORGET_X), PGY(FORGET_Y(r))) != UPD_TAP_FORGET + r) WF("H: row %d's FORGET reads %d", r, updates_page_tap(PGX(FORGET_X), PGY(FORGET_Y(r))));
        if (updates_page_tap(PGX(UPD_FORGET_X - 12), PGY(FORGET_Y(r))) != UPD_TAP_NONE) WF("H: a network's name is a FORGET (row %d)", r);
        for (int y = UPD_FORGET_Y + r * UPD_NET_DY - 3; y < UPD_FORGET_Y + (r + 1) * UPD_NET_DY - 3; y++)
            if (updates_page_tap(PGX(FORGET_X), PGY(y)) != UPD_TAP_FORGET + r) WF("H: page y %d reads %d, not row %d's FORGET", y, updates_page_tap(PGX(FORGET_X), PGY(y)), r);
    }
    if (updates_page_tap(PGX(FORGET_X), PGY(UPD_FORGET_Y + 3 * UPD_NET_DY)) != UPD_TAP_NONE) WF("H: a FORGET under the third row");
    if (updates_page_tap(PGX(UPD_X + UPD_W / 2), PGY(UPD_ADD_Y + UPD_BTN40 / 2)) != UPD_TAP_ADD) WF("H: ADD A NETWORK");
    if (updates_page_tap(PGX(UPD_CLOSE_X + UPD_CLOSE_W / 2), PGY(UPD_CLOSE_Y + UPD_CLOSE_H / 2)) != UPD_TAP_NETS_BACK) WF("H: the WI-FI page's BACK");
    updates_page_open();                                     /* the UPDATES face */
    if (updates_page_tap(PGX(UPD_CHECK_X + UPD_CHECK_W / 2), PGY(UPD_CHECK_Y + UPD_CHECK_H / 2)) != UPD_TAP_CHECK) WF("H: CHECK FOR UPDATES");
    if (updates_page_tap(PGX(UPD_CHECK_X + UPD_CHECK_W / 2), PGY(UPD_CLOSE_Y)) == UPD_TAP_CHECK) WF("H: CHECK FOR UPDATES answers in the foot's band");
    for (int rr = 0; rr < 3; rr++) if (updates_page_tap(PGX(FORGET_X), PGY(FORGET_Y(rr))) >= UPD_TAP_FORGET) WF("H: a FORGET answers on the UPDATES face (row %d)", rr);
    NETS_OPEN();
    int r = UPT(1, 1000000, 1080000);
    if (r != UPD_TAP_NONE || updates_forget_row() != 1 || !updates_forget_armed() || net_port_creds_count() != 3) WF("H: one tap on row 2's FORGET: returned %d, armed row %d, %d saved", r, updates_forget_row(), net_port_creds_count());
    r = UPT(1, 1180000, 1680000);                            /* its double (THE TOUCH RULE): spent */
    if (r != UPD_TAP_NONE || net_port_creds_count() != 3 || updates_forget_row() != 1) WF("H: a doubled tap forgot a network");
    r = UPT(2, 3000000, 3080000);                            /* another row's FORGET: that one arms, nothing is forgotten */
    if (r != UPD_TAP_NONE || updates_forget_row() != 2 || net_port_creds_count() != 3) WF("H: SURE? on row 2, then row 3's FORGET: returned %d, armed row %d, %d saved (want row 3 armed, 3 saved)", r, updates_forget_row(), net_port_creds_count());
    r = UPT(2, 3800000, 3880000);
    if (r != UPD_TAP_FORGET_3 || updates_forget_armed() || !order_is(net_port_nets(), "Home", QUOTED, NULL)) WF("H: SURE? on row 3: returned %d, %d saved, first %s (want the long name gone, Home and the quoted one kept in order)", r, net_port_creds_count(), net_port_nets()->net[0].ssid);
    r = UPT(0, 6000000, 6080000);
    updates_page_touch(PGX(UPD_FORGET_X - 60), PGY(FORGET_Y(0)), true, 6500000); updates_page_touch(PGX(UPD_FORGET_X - 60), PGY(FORGET_Y(0)), false, 6580000);   /* a tap on the name: not armed any more */
    if (updates_forget_armed()) WF("H: a tap elsewhere left SURE? armed");
    r = UPT(0, 7000000, 7080000); r = UPT(0, 7800000, 7880000);
    if (r != UPD_TAP_FORGET || !order_is(net_port_nets(), QUOTED, NULL, NULL)) WF("H: row 1 forgotten: returned %d, %d saved", r, net_port_creds_count());
    /* ARMED BY NAME (review 6): the list changes while SURE? is up (the director's wifi forget) - SURE? stays with its
       network, a tap where it used to be arms what stands there now, and a network that left the list is not armed */
    three("Home", QUOTED, LONG32);
    NETS_OPEN();
    r = UPT(1, 10000000, 10080000);
    if (updates_forget_row() != 1) WF("H: (row 2 not armed)");
    net_port_creds_forget_one("Home");                       /* the quoted one is row 1 now */
    if (updates_forget_row() != 0 || !updates_forget_armed()) WF("H: the list lost its first network while SURE? was up on the second: SURE? is on row %d (want row 1, with its network)", updates_forget_row() + 1);
    render_updates_page(fb, TANK_W);
    r = UPT(1, 10800000, 10880000);                          /* where SURE? was: the long name stands there now */
    if (r != UPD_TAP_NONE || net_port_creds_count() != 2 || updates_forget_row() != 1) WF("H: a tap where SURE? used to be: returned %d, %d saved, armed row %d (want nothing forgotten, that row armed)", r, net_port_creds_count(), updates_forget_row() + 1);
    net_port_creds_forget_one(LONG32);                       /* the armed one leaves the list */
    if (updates_forget_armed() || updates_forget_row() != -1) WF("H: the armed network left the list and SURE? is still up (row %d)", updates_forget_row() + 1);
    r = UPT(0, 11600000, 11680000);
    if (r != UPD_TAP_NONE || net_port_creds_count() != 1 || updates_forget_row() != 0) WF("H: after that, one tap on the last network's FORGET: returned %d, %d saved (want it armed, not forgotten)", r, net_port_creds_count());
    updates_page_open();

    /* ---- I: on this board's glass ---- */
    three(LONG32, QUOTED, "Fishbowl");
    update_begin(50, false); ADV(2); ADV(2); net_port_off();          /* (the long name joined: LAST USED under it) */
    if (!net_port_nets()->net[0].joined || strcmp(net_port_nets()->net[0].ssid, LONG32)) WF("I: (the long name is not the one used last)");
    for (int pass_i = 0; pass_i < 4; pass_i++) {              /* three rows, one armed in turn */
        NETS_OPEN();
        if (pass_i) UPT(pass_i - 1, 1000000, 1080000);
        render_updates_page(fb, TANK_W);
        int bx = 0, by = 0; const char *bad = frame_fits(3, &bx, &by);
        if (bad) WF("I: the UPDATES page with three networks (%s) draws %s at frame %d,%d", pass_i ? "one armed" : "none armed", bad, bx, by);
    }
    NETS_OPEN();
    for (int n = 2; n >= 0; n--) {                            /* two, one, none saved: the EMPTY places; and the UPDATES face, its door's words */
        net_port_creds_at(n, ssid, NULL); net_port_creds_forget_one(ssid);
        updates_page_show_nets(true); render_updates_page(fb, TANK_W);
        int bx = 0, by = 0; const char *bad = frame_fits(n, &bx, &by);
        if (bad) WF("I: the WI-FI page with %d saved draws %s at frame %d,%d", n, bad, bx, by);
        updates_page_show_nets(false); render_updates_page(fb, TANK_W);
        bad = frame_fits(0, &bx, &by);
        if (bad) WF("I: the UPDATES page with %d saved draws %s at frame %d,%d", n, bad, bx, by);
    }
    store_reset(); remove(nets_file);
    printf("selftest-wifi3: three saved in the tank's order, a fourth replaces the one used longest ago, a saved name again takes the password alone; "
           "the pick is the most recently used in range; 0.4.1's network moves over and survives a cut at every step; update mode tries the last one first with no scan, "
           "then scans once; THREE NETWORKS SAVED before a fourth; a FORGET per row, armed per row; all of it on this glass (%d x %d)\n", TANK_W, TANK_H);
    printf("selftest-wifi3: PASS\n");
    return 0;
}

/* the pictures (sim/sheets wifi042_*): --wifi3-shot <prefix> */
int wifi3_shot(const char *prefix) {
    paths("shot");
    store_reset();
    setenv("POCKET_TANK_BOARD_CODE", "KM4T-7RXA", 1);
    #define JOINED() do { update_begin(50, false); ADV(2); ADV(2); net_port_off(); } while (0)   /* the first one joined: LAST USED */
    net_port_creds_set("Fishbowl", "password-1"); JOINED();
    updates_page_open(); updates_page_show_nets(true); render_updates_page(fb, TANK_W); ppm(prefix, "updates_1");   /* (0.4.2: the WI-FI face has the rows) */
    net_port_creds_set("Strato's Wi-Fi", "password-1"); JOINED();
    render_updates_page(fb, TANK_W); ppm(prefix, "updates_2");
    net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1"); JOINED();
    render_updates_page(fb, TANK_W); ppm(prefix, "updates_3");
    updates_page_touch(PGX(FORGET_X), PGY(FORGET_Y(1)), true, 1000000); updates_page_touch(PGX(FORGET_X), PGY(FORGET_Y(1)), false, 1080000);
    render_updates_page(fb, TANK_W); ppm(prefix, "updates_forget_sure");
    updates_page_open(); updates_page_show_nets(true);
    net_port_creds_forget_one("Fishbowl"); net_port_creds_set(LONG32, "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1"); JOINED();
    render_updates_page(fb, TANK_W); ppm(prefix, "updates_name32");
    net_port_creds_set(LONG32, "password-1"); JOINED();      /* the long one used last */
    render_updates_page(fb, TANK_W); ppm(prefix, "updates_name32_last");
    /* the wizard, three saved, a fourth picked */
    net_port_creds_forget(); net_port_creds_set("Fishbowl", "password-1"); net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1");
    { static const char *const see[] = { "Lakeside Cabin", "Neighbor 5G", "CoffeeShop Guest" }; net_sim_see(see, 3); }
    update_begin(50, false); ADV(2); ADV(1.5f);
    render_update(fb, TANK_W, 0.3f); ppm(prefix, "none_in_range");
    to_list(); TAP(PAGE_W / 4, ROWY(0));
    render_update(fb, TANK_W, 0.3f); ppm(prefix, "replace");
    net_port_off();
    net_port_creds_forget(); net_port_creds_set(LONG32, "password-1"); net_port_creds_set("Grandma's House", "password-1"); net_port_creds_set("Strato's Wi-Fi", "password-1");
    update_begin(50, false); ADV(2); ADV(1.5f); to_list(); TAP(PAGE_W / 4, ROWY(0));
    render_update(fb, TANK_W, 0.3f); ppm(prefix, "replace_name32");
    net_port_off(); net_sim_see(NULL, 0); store_reset();
    printf("wifi3-shot: %s_*.ppm\n", prefix);
    return 0;
}
