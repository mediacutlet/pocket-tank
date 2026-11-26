/* net_port_sim.c - the update flow's net port (common/update.h) with
 * pretend networks, so the pages are built and self-tested on the desktop.
 * Nothing is downloaded and nothing is written.
 *
 * Time is fed by the sim (net_sim_advance(dt) once a frame; a selftest feeds
 * seconds at a time), so a test walks the flow deterministically.
 *
 *   the store       THE SAVED NETWORKS (0.4.2, common/wifi_nets.h): <file>.nets,
 *                   the list's 299 bytes as the boards' NVS blob "nets" holds
 *                   them, written whole (a new file renamed over the old).
 *                   <file> itself - POCKET_TANK_WIFI, else
 *                   ~/.cache/pocket-tank/wifi.txt: ssid on line 1, password
 *                   on 2, the learned channel on 3 if any - is 0.4.1's ONE
 *                   network (the boards' old ssid / pass / chan keys):
 *                   whenever it is found it is added to the list, the list
 *                   is written, and then it is removed - the boards' rule
 *                   and order (wifi_nets.h, 0.4.1'S KEYS)
 *   the scan        nine networks in 1.2 s, two pages' worth; "Ghost" is out
 *                   of range on connect, a password starting "wrong" is refused.
 *                   net_sim_see(names, n) replaces what the radio sees (a
 *                   selftest's own street): the scan lists those, and a name
 *                   not among them is out of range on connect too
 *   the check       POCKET_TANK_FAKE_UPDATE: "0.9.9" (default: an update is
 *                   offered), "none" (up to date), "cable" (needs the cable),
 *                   "fail" (no answer), or any release number
 *   the install     6 s of progress; POCKET_TANK_FAKE_UPDATE=downloadfail
 *                   stops it at 40% */
#include "update.h"
#include "wifi_nets.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *creds_path(void) {
    static char p[512];
    if (getenv("POCKET_TANK_WIFI")) return getenv("POCKET_TANK_WIFI");
    snprintf(p, sizeof p, "%s/.cache/pocket-tank/wifi.txt", getenv("HOME") ? getenv("HOME") : ".");
    return p;
}
static void chomp(char *s) { size_t n = strlen(s); while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0; }
static const char *nets_path(void) { static char p[560]; snprintf(p, sizeof p, "%s.nets", creds_path()); return p; }
/* A POWER CUT, for the selftest: every change to the store is a few steps in a fixed order (the boards' order,
 * net_port_esp.c); net_sim_cut_at(k) lets k - 1 more of them happen and drops the k-th and every one after it,
 * until net_sim_cut_at(0). A step: the list written, the list removed, 0.4.1's file removed. */
static int s_cut, s_steps;
void net_sim_cut_at(int k) { s_cut = k; s_steps = 0; }
static bool step(void) { return !s_cut || ++s_steps < s_cut; }
static int s_miss;                                    /* the quick look's count (the boards' NVS "miss"): boots that saw none of them */
int  net_sim_miss(void) { return s_miss; }
void net_sim_miss_set(int v) { s_miss = v; }
static bool legacy_read(char *ssid, char *pass, int *chan) {
    FILE *f = fopen(creds_path(), "r"); if (!f) return false;
    char a[NET_SSID_MAX + 8] = "", b[NET_PASS_MAX + 8] = "", c[16] = "";
    bool ok = fgets(a, sizeof a, f) != NULL; if (ok) { chomp(a); if (!fgets(b, sizeof b, f)) b[0] = 0; chomp(b); if (!fgets(c, sizeof c, f)) c[0] = 0; }
    fclose(f);
    if (!ok || !a[0]) return false;
    snprintf(ssid, NET_SSID_MAX + 1, "%s", a); snprintf(pass, NET_PASS_MAX + 1, "%s", b); *chan = atoi(c);
    return true;
}
static bool list_write(const wifi_nets_t *w) {         /* whole or not at all */
    if (!step()) return false;
    char tmp[600]; snprintf(tmp, sizeof tmp, "%s.tmp", nets_path());
    FILE *f = fopen(tmp, "wb"); if (!f) return false;
    bool ok = fwrite(w, 1, sizeof *w, f) == sizeof *w; ok = fclose(f) == 0 && ok;
    return ok && rename(tmp, nets_path()) == 0;
}
static wifi_nets_t s_w;                               /* the port's copy: read again from the files at every ask (a selftest moves them) */
static bool nets_read(wifi_nets_t *w) {
    unsigned char raw[sizeof *w + 1]; size_t n = 0;
    FILE *f = fopen(nets_path(), "rb");
    if (f) { n = fread(raw, 1, sizeof raw, f); fclose(f); }
    if (!f || !wifi_nets_from_bytes(w, raw, n)) wifi_nets_clear(w);   /* no list, or one that is not whole */
    /* 0.4.1's network, whenever it is found (wifi_nets.h, 0.4.1'S KEYS): into the list, the list written, THEN its
       file removed. A cut before the write: the next read does this again; between the two: the same add again */
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1]; int chan = 0;
    if (legacy_read(ssid, pass, &chan) && wifi_nets_take_old(w, ssid, pass, chan) && list_write(w) && step()) remove(creds_path());
    return true;
}
const wifi_nets_t *net_port_nets(void) { nets_read(&s_w); return &s_w; }
static bool nets_save(const wifi_nets_t *w) { return list_write(w); }   /* (0.4.1's file is gone by now: every change reads first) */
static bool joined_now(const char *ssid);
bool net_port_creds_set(const char *ssid, const char *pass) {
    wifi_nets_t w; char gone[NET_SSID_MAX + 1];
    if (!nets_read(&w) || !wifi_nets_add(&w, ssid, pass, gone)) return false;
    if (joined_now(ssid)) wifi_nets_joined(&w, ssid, 6);   /* the wizard saves the one it is on */
    if (!nets_save(&w)) return false;
    s_miss = 0;
    return true;
}
bool net_port_creds_forget_one(const char *ssid) {
    wifi_nets_t w;
    if (!nets_read(&w) || !wifi_nets_forget_one(&w, ssid)) return false;
    if (!nets_save(&w)) return false;
    s_miss = 0;
    return true;
}
static int s_forget_refuse;                          /* a selftest: the next n forgets are refused (the store would not open) */
void net_sim_forget_refuse(int n) { s_forget_refuse = n; }
bool net_port_creds_forget(void) {                    /* every one. 0.4.1's file FIRST: cut between the two, the list still stands whole
                                                         (nothing forgotten yet) - the other order would bring the old network back.
                                                         false = refused, or cut short: a network is still there */
    if (s_forget_refuse > 0) { s_forget_refuse--; return false; }
    bool a = step(); if (a) remove(creds_path());
    bool b = step(); if (b) remove(nets_path());
    s_miss = 0;
    return a && b;
}

/* the board code: POCKET_TANK_BOARD_CODE (pictures), else made once into
 * POCKET_TANK_BOARD_CODE_FILE (selftests) or ~/.cache/pocket-tank/board_code.txt.
 * The crew answer: POCKET_TANK_CREW=1 (pictures), else what the last pretend
 * check said - POCKET_TANK_FAKE_CREW at the check: 1 on the list, 0 not on
 * it, unset = no clear answer (the last one stands, as on the board) */
static int s_crew;
bool net_port_board_code(char out[NET_CODE_LEN + 1]) {
    const char *e = getenv("POCKET_TANK_BOARD_CODE");
    if (e && strlen(e) == NET_CODE_LEN) { snprintf(out, NET_CODE_LEN + 1, "%s", e); return true; }
    char p[512];
    if (getenv("POCKET_TANK_BOARD_CODE_FILE")) snprintf(p, sizeof p, "%s", getenv("POCKET_TANK_BOARD_CODE_FILE"));
    else snprintf(p, sizeof p, "%s/.cache/pocket-tank/board_code.txt", getenv("HOME") ? getenv("HOME") : ".");
    FILE *f = fopen(p, "r");
    if (f) { char b[32] = ""; bool ok = fgets(b, sizeof b, f) != NULL; fclose(f); chomp(b);
             if (ok && strlen(b) == NET_CODE_LEN) { snprintf(out, NET_CODE_LEN + 1, "%s", b); return true; } }
    static const char A[] = NET_CODE_ALPHABET;
    FILE *r = fopen("/dev/urandom", "rb"); unsigned char x[8] = { 0 };
    if (!r || fread(x, 1, 8, r) != 8) { if (r) fclose(r); return false; }
    fclose(r);
    for (int i = 0, k = 0; i < NET_CODE_LEN; i++) out[i] = i == 4 ? '-' : A[x[k++] % (sizeof A - 1)];
    out[NET_CODE_LEN] = 0;
    if (!(f = fopen(p, "w"))) return false;
    fprintf(f, "%s\n", out); fclose(f); return true;
}
int net_port_crew(void) { const char *e = getenv("POCKET_TANK_CREW"); return e ? e[0] == '1' : s_crew; }

/* ---- the radio ---- */
enum { OP_NONE, OP_SCAN, OP_CONNECT, OP_CHECK, OP_INSTALL };
static int   s_op, s_state, s_err;
static float s_t;                                    /* seconds into the operation */
static char  s_ssid[NET_SSID_MAX + 1], s_pass[NET_PASS_MAX + 1];
static update_manifest_t s_m;
static int   s_percent;
static bool  s_radio;

static wifi_names_t s_saw;                             /* the saved networks the last scan heard, BY NAME, fixed when the scan ends (the boards' s_saw) */
static int s_cold;                                    /* net_sim_cold(n): the next n connects find nobody (a cold radio's first sweep misses a router) */
void net_sim_cold(int n) { s_cold = n; }
static const char *const *s_see; static int s_n_see = -1;   /* net_sim_see: what the radio sees instead of FAKE_APS (-1 = FAKE_APS) */
void net_sim_see(const char *const *names, int n) { s_see = names; s_n_see = names ? n : -1; }
static bool in_range(const char *ssid) {
    if (s_n_see < 0) return true;                    /* (the pretend street: only "Ghost" is out of range, below) */
    for (int i = 0; i < s_n_see; i++) if (!strcmp(s_see[i], ssid)) return true;
    return false;
}
static const net_ap_t FAKE_APS[] = {
    { "Fishbowl", -48, true }, { "Strato's Wi-Fi", -60, true }, { "CoffeeShop Guest", -70, false },
    { "Apartment 3B", -66, true }, { "Neighbor 5G", -72, true }, { "Printer-Setup", -78, false },
    { "xfinitywifi", -80, false }, { "Ghost", -85, true }, { "Basement", -88, true },
};
#define N_FAKE ((int)(sizeof FAKE_APS / sizeof FAKE_APS[0]))

void net_sim_advance(float dt) {                     /* the sim's clock */
    if (s_state != NET_BUSY) return;
    s_t += dt;
    const char *fake = getenv("POCKET_TANK_FAKE_UPDATE");
    switch (s_op) {
    case OP_SCAN:
        if (s_t >= 1.2f) {
            s_state = NET_DONE;
            const wifi_nets_t *w = net_port_nets(); wifi_names_clear(&s_saw);   /* everything heard, not only the list's first NET_SCAN_MAX */
            if (s_n_see >= 0) for (int i = 0; i < s_n_see; i++) wifi_nets_heard(w, &s_saw, s_see[i]);
            else for (int i = 0; i < N_FAKE; i++) wifi_nets_heard(w, &s_saw, FAKE_APS[i].ssid);
        }
        break;
    case OP_CONNECT:
        if (s_t >= 1.5f) {
            if (s_cold > 0) { s_cold--; s_state = NET_FAILED; s_err = NET_ERR_NO_SIGNAL; }
            else if (!strcmp(s_ssid, "Ghost") || !in_range(s_ssid)) { s_state = NET_FAILED; s_err = NET_ERR_NO_SIGNAL; }
            else if (!strncmp(s_pass, "wrong", 5)) { s_state = NET_FAILED; s_err = NET_ERR_PASSWORD; }
            else {
                s_state = NET_DONE; s_miss = 0;
                wifi_nets_t w;                           /* a saved one joined: it is the one tried first from now on (the boards' chan_learn) */
                if (nets_read(&w) && wifi_nets_joined(&w, s_ssid, 6)) nets_save(&w);
            }
        }
        break;
    case OP_CHECK:
        if (s_t >= 1.0f) {
            { const char *fc = getenv("POCKET_TANK_FAKE_CREW"); if (fc && (fc[0] == '0' || fc[0] == '1')) s_crew = fc[0] == '1'; }
            memset(&s_m, 0, sizeof s_m);
            const char *rel = fake && *fake ? fake : "0.9.9";
            if (!strcmp(rel, "fail")) { s_state = NET_FAILED; s_err = NET_ERR_NO_NET; break; }
            if (!strcmp(rel, "none")) rel = PT_RELEASE;
            if (!strcmp(rel, "cable")) { rel = "0.9.9"; s_m.needs_cable = true; }
            if (!strcmp(rel, "downloadfail") || !strcmp(rel, "boardimage")) rel = "0.9.9";
            snprintf(s_m.board, sizeof s_m.board, "%s", update_target_board());   /* the URL names the board (0.3.9.3: the one it IS) */
            if (!strcmp(rel, "otherboard")) { rel = "0.9.9"; snprintf(s_m.board, sizeof s_m.board, "%s", strcmp(update_target_board(), "amoled18") ? "amoled18" : "round175c"); }
            int a = 0, b = 0, c = 0, d = 0; sscanf(rel, "%d.%d.%d.%d", &a, &b, &c, &d);
            if (d) snprintf(s_m.release, sizeof s_m.release, "%d.%d.%d.%d", a, b, c, d);
            else   snprintf(s_m.release, sizeof s_m.release, "%d.%d.%d", a, b, c);
            s_m.release_num = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;   /* version.h: four bytes since 0.3.8.1 */
            snprintf(s_m.build, sizeof s_m.build, "fake123");
            snprintf(s_m.note, sizeof s_m.note, "A pretend update: the sim has no radio and writes nothing.");
            s_m.app_size = 1900000;
            s_state = NET_DONE;
        }
        break;
    case OP_INSTALL:
        s_percent = (int)(s_t / 6.0f * 100); if (s_percent > 100) s_percent = 100;
        if (fake && !strcmp(fake, "downloadfail") && s_percent >= 40) { s_state = NET_FAILED; s_err = NET_ERR_DOWNLOAD; }
        else if (fake && !strcmp(fake, "boardimage") && s_percent >= 2) { s_state = NET_FAILED; s_err = NET_ERR_BOARD; }   /* the image's marker, read at its first sector */
        else if (s_t >= 6.0f) s_state = NET_DONE;
        break;
    default: break;
    }
}
static void begin(int op) { s_op = op; s_state = NET_BUSY; s_err = NET_ERR_NONE; s_t = 0; s_radio = true; }
void net_port_scan_start(void) { begin(OP_SCAN); }
int  net_port_scan_state(void) { return s_op == OP_SCAN ? s_state : NET_IDLE; }
int  net_port_scan_results(net_ap_t *out, int max) {
    if (s_n_see >= 0) {
        int n = s_n_see < max ? s_n_see : max;
        for (int i = 0; i < n; i++) { memset(&out[i], 0, sizeof out[i]); snprintf(out[i].ssid, sizeof out[i].ssid, "%s", s_see[i]); out[i].rssi = -48 - 6 * i; out[i].secured = true; }
        return n;
    }
    int n = N_FAKE < max ? N_FAKE : max;
    for (int i = 0; i < n; i++) out[i] = FAKE_APS[i];
    return n;
}
int  net_port_scan_saved(const wifi_names_t *tried, char *ssid, char *pass) {   /* as the boards: what the scan heard stands; the list is read NOW */
    const wifi_nets_t *w = net_port_nets(); const int i = wifi_nets_next(w, &s_saw, tried);
    if (i >= 0) { snprintf(ssid, NET_SSID_MAX + 1, "%s", w->net[i].ssid); snprintf(pass, NET_PASS_MAX + 1, "%s", w->net[i].pass); }
    return i;
}
void net_port_connect_start(const char *ssid, const char *pass) {
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid); snprintf(s_pass, sizeof s_pass, "%s", pass); begin(OP_CONNECT);
}
int  net_port_connect_state(void) { return s_op == OP_CONNECT ? s_state : NET_IDLE; }
void net_port_check_start(void) { begin(OP_CHECK); }
int  net_port_check_state(const update_manifest_t **m) { if (s_op != OP_CHECK) return NET_IDLE; if (s_state == NET_DONE) *m = &s_m; return s_state; }
void net_port_install_start(void) { s_percent = 0; begin(OP_INSTALL); }
int  net_port_install_state(int *percent) { if (s_op != OP_INSTALL) return NET_IDLE; *percent = s_percent; return s_state; }
void net_port_abort(void) { if (s_state == NET_BUSY) { s_state = NET_FAILED; s_err = NET_ERR_ABORTED; } }
void net_port_off(void) { s_op = OP_NONE; s_state = NET_IDLE; s_radio = false; }
int  net_port_fail_reason(void) { return s_err; }
bool net_sim_radio_on(void) { return s_radio; }
static bool joined_now(const char *ssid) { return s_op == OP_CONNECT && s_state == NET_DONE && !strcmp(s_ssid, ssid); }
