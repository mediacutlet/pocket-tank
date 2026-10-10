/* net_port_sim.c - the update flow's net port (common/update.h) with
 * pretend networks, so the pages are built and self-tested on the desktop.
 * Nothing is downloaded and nothing is written.
 *
 * Time is fed by the sim (net_sim_advance(dt) once a frame; a selftest feeds
 * seconds at a time), so a test walks the flow deterministically.
 *
 *   the store       AQUA_PETS_WIFI (a file: ssid on line 1, password on 2),
 *                   else ~/.cache/aqua-pets/wifi.txt
 *   the scan        nine networks in 1.2 s, two pages' worth; "Ghost" is out
 *                   of range on connect, a password starting "wrong" is refused
 *   the check       AQUA_PETS_FAKE_UPDATE: "0.9.9" (default: an update is
 *                   offered), "none" (up to date), "cable" (needs the cable),
 *                   "fail" (no answer), or any release number
 *   the install     6 s of progress; AQUA_PETS_FAKE_UPDATE=downloadfail
 *                   stops it at 40% */
#include "update.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *creds_path(void) {
    static char p[512];
    if (getenv("AQUA_PETS_WIFI")) return getenv("AQUA_PETS_WIFI");
    snprintf(p, sizeof p, "%s/.cache/aqua-pets/wifi.txt", getenv("HOME") ? getenv("HOME") : ".");
    return p;
}
static void chomp(char *s) { size_t n = strlen(s); while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0; }
bool net_port_creds_get(char *ssid, char *pass) {
    FILE *f = fopen(creds_path(), "r"); if (!f) return false;
    char a[NET_SSID_MAX + 8] = "", b[NET_PASS_MAX + 8] = "";
    bool ok = fgets(a, sizeof a, f) != NULL; if (ok) { chomp(a); if (!fgets(b, sizeof b, f)) b[0] = 0; chomp(b); }
    fclose(f);
    if (!ok || !a[0]) return false;
    snprintf(ssid, NET_SSID_MAX + 1, "%s", a); snprintf(pass, NET_PASS_MAX + 1, "%s", b);
    return true;
}
bool net_port_creds_set(const char *ssid, const char *pass) {
    FILE *f = fopen(creds_path(), "w"); if (!f) return false;
    fprintf(f, "%s\n%s\n", ssid, pass); fclose(f); return true;
}
void net_port_creds_forget(void) { remove(creds_path()); }

/* ---- the radio ---- */
enum { OP_NONE, OP_SCAN, OP_CONNECT, OP_CHECK, OP_INSTALL };
static int   s_op, s_state, s_err;
static float s_t;                                    /* seconds into the operation */
static char  s_ssid[NET_SSID_MAX + 1], s_pass[NET_PASS_MAX + 1];
static update_manifest_t s_m;
static int   s_percent;
static bool  s_radio;

static const net_ap_t FAKE_APS[] = {
    { "Fishbowl", -48, true }, { "Strato's Wi-Fi", -60, true }, { "CoffeeShop Guest", -70, false },
    { "Apartment 3B", -66, true }, { "Neighbor 5G", -72, true }, { "Printer-Setup", -78, false },
    { "xfinitywifi", -80, false }, { "Ghost", -85, true }, { "Basement", -88, true },
};
#define N_FAKE ((int)(sizeof FAKE_APS / sizeof FAKE_APS[0]))

void net_sim_advance(float dt) {                     /* the sim's clock */
    if (s_state != NET_BUSY) return;
    s_t += dt;
    const char *fake = getenv("AQUA_PETS_FAKE_UPDATE");
    switch (s_op) {
    case OP_SCAN:    if (s_t >= 1.2f) s_state = NET_DONE; break;
    case OP_CONNECT:
        if (s_t >= 1.5f) {
            if (!strcmp(s_ssid, "Ghost")) { s_state = NET_FAILED; s_err = NET_ERR_NO_SIGNAL; }
            else if (!strncmp(s_pass, "wrong", 5)) { s_state = NET_FAILED; s_err = NET_ERR_PASSWORD; }
            else s_state = NET_DONE;
        }
        break;
    case OP_CHECK:
        if (s_t >= 1.0f) {
            memset(&s_m, 0, sizeof s_m);
            const char *rel = fake && *fake ? fake : "0.9.9";
            if (!strcmp(rel, "fail")) { s_state = NET_FAILED; s_err = NET_ERR_NO_NET; break; }
            if (!strcmp(rel, "none")) rel = PT_RELEASE;
            if (!strcmp(rel, "cable")) { rel = "0.9.9"; s_m.needs_cable = true; }
            if (!strcmp(rel, "downloadfail") || !strcmp(rel, "boardimage")) rel = "0.9.9";
            snprintf(s_m.board, sizeof s_m.board, "%s", PT_BOARD);
            if (!strcmp(rel, "otherboard")) { rel = "0.9.9"; snprintf(s_m.board, sizeof s_m.board, "%s", strcmp(PT_BOARD, "amoled18") ? "amoled18" : "round175c"); }
            int a = 0, b = 0, c = 0; sscanf(rel, "%d.%d.%d", &a, &b, &c);
            snprintf(s_m.release, sizeof s_m.release, "%d.%d.%d", a, b, c);
            s_m.release_num = ((uint32_t)a << 16) | ((uint32_t)b << 8) | (uint32_t)c;
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
    int n = N_FAKE < max ? N_FAKE : max;
    for (int i = 0; i < n; i++) out[i] = FAKE_APS[i];
    return n;
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
