/* wifi_nets.c - the saved networks: see wifi_nets.h. The first half is pure; the second is the store's calls every
 * platform shares, over the port's one (net_port_nets). */
#include "wifi_nets.h"
#include <stdio.h>
#include <string.h>

void wifi_nets_clear(wifi_nets_t *w) { memset(w, 0, sizeof *w); w->ver = WIFI_NETS_VER; }
bool wifi_nets_from_bytes(wifi_nets_t *w, const void *bytes, size_t len) {
    const wifi_nets_t *b = bytes;
    bool ok = b && len == sizeof *w && b->ver == WIFI_NETS_VER && b->n <= WIFI_NETS_MAX;
    for (int i = 0; ok && i < b->n; i++) {
        const wifi_net_t *e = &b->net[i];
        if (!e->ssid[0] || !memchr(e->ssid, 0, sizeof e->ssid) || !memchr(e->pass, 0, sizeof e->pass)) ok = false;
        for (int k = 0; ok && k < i; k++) if (!strcmp(b->net[k].ssid, e->ssid)) ok = false;
    }
    if (!ok) { wifi_nets_clear(w); return false; }
    if ((const void *)w != bytes) memcpy(w, b, sizeof *w);
    for (int i = 0; i < w->n; i++) {                         /* (a channel is 1..14, 0 = not known; joined is 0 or 1) */
        if (w->net[i].chan > 14) w->net[i].chan = 0;
        w->net[i].joined = w->net[i].joined != 0;
    }
    for (int i = w->n; i < WIFI_NETS_MAX; i++) memset(&w->net[i], 0, sizeof w->net[i]);   /* (nothing rides in a free place) */
    return true;
}
static void put(wifi_net_t *e, const char *ssid, const char *pass, int chan, bool joined) {
    memset(e, 0, sizeof *e);
    snprintf(e->ssid, sizeof e->ssid, "%s", ssid); snprintf(e->pass, sizeof e->pass, "%s", pass ? pass : "");
    e->chan = chan >= 1 && chan <= 14 ? (uint8_t)chan : 0; e->joined = joined;
}
int wifi_nets_find(const wifi_nets_t *w, const char *ssid) {
    if (!ssid || !ssid[0]) return -1;
    for (int i = 0; i < w->n; i++) if (!strncmp(w->net[i].ssid, ssid, NET_SSID_MAX)) return i;
    return -1;
}
int wifi_nets_victim(const wifi_nets_t *w, const char *ssid) {
    return w->n >= WIFI_NETS_MAX && ssid && ssid[0] && wifi_nets_find(w, ssid) < 0 ? WIFI_NETS_MAX - 1 : -1;
}
static void to_front(wifi_nets_t *w, int i) {                /* net[i] first, the ones before it one place back, their order kept */
    if (i <= 0) return;
    wifi_net_t e = w->net[i];
    memmove(&w->net[1], &w->net[0], (size_t)i * sizeof e);
    w->net[0] = e;
    memset(&e, 0, sizeof e);
}
bool wifi_nets_add(wifi_nets_t *w, const char *ssid, const char *pass, char *replaced) {
    if (replaced) replaced[0] = 0;
    if (!ssid || !ssid[0]) return false;
    int i = wifi_nets_find(w, ssid);
    if (i >= 0) {                                            /* saved already: the new password, no new place */
        memset(w->net[i].pass, 0, sizeof w->net[i].pass);
        snprintf(w->net[i].pass, sizeof w->net[i].pass, "%s", pass ? pass : "");
        to_front(w, i);
        return true;
    }
    if (w->n >= WIFI_NETS_MAX) {                             /* full: the one used longest ago goes */
        if (replaced) snprintf(replaced, NET_SSID_MAX + 1, "%s", w->net[WIFI_NETS_MAX - 1].ssid);
        w->n = WIFI_NETS_MAX - 1;
    }
    memmove(&w->net[1], &w->net[0], (size_t)w->n * sizeof w->net[0]);
    put(&w->net[0], ssid, pass, 0, false);
    w->n++;
    return true;
}
bool wifi_nets_joined(wifi_nets_t *w, const char *ssid, int chan) {
    int i = wifi_nets_find(w, ssid);
    if (i < 0) return false;
    wifi_net_t *e = &w->net[i];
    bool moved = i != 0 || !e->joined;
    if (chan >= 1 && chan <= 14 && e->chan != chan) { e->chan = (uint8_t)chan; moved = true; }
    e->joined = 1;
    to_front(w, i);
    return moved;
}
bool wifi_nets_forget_one(wifi_nets_t *w, const char *ssid) {
    int i = wifi_nets_find(w, ssid);
    if (i < 0) return false;
    memmove(&w->net[i], &w->net[i + 1], (size_t)(w->n - 1 - i) * sizeof w->net[0]);
    w->n--;
    memset(&w->net[w->n], 0, sizeof w->net[0]);              /* the password does not stay behind in the free place */
    return true;
}
bool wifi_nets_take_old(wifi_nets_t *w, const char *ssid, const char *pass, int chan) {
    if (!wifi_nets_add(w, ssid, pass, NULL)) return false;
    if (chan >= 1 && chan <= 14) wifi_nets_joined(w, ssid, chan);
    return true;
}
void wifi_names_clear(wifi_names_t *s) { memset(s, 0, sizeof *s); }
bool wifi_names_has(const wifi_names_t *s, const char *ssid) {
    for (int i = 0; i < s->n && i < WIFI_NETS_MAX; i++) if (!strncmp(s->ssid[i], ssid, NET_SSID_MAX)) return true;
    return false;
}
void wifi_names_add(wifi_names_t *s, const char *ssid) {
    if (!ssid || !ssid[0] || s->n >= WIFI_NETS_MAX || wifi_names_has(s, ssid)) return;
    snprintf(s->ssid[s->n++], NET_SSID_MAX + 1, "%s", ssid);
}
int wifi_nets_heard(const wifi_nets_t *w, wifi_names_t *seen, const char *name) {
    if (!name || wifi_nets_find(w, name) < 0 || wifi_names_has(seen, name) || seen->n >= WIFI_NETS_MAX) return -1;
    wifi_names_add(seen, name);
    return seen->n - 1;
}
int wifi_nets_next(const wifi_nets_t *w, const wifi_names_t *seen, const wifi_names_t *tried) {
    for (int i = 0; i < w->n; i++) if (wifi_names_has(seen, w->net[i].ssid) && !(tried && wifi_names_has(tried, w->net[i].ssid))) return i;
    return -1;
}
int wifi_nets_left(const wifi_nets_t *w, const wifi_names_t *seen, const wifi_names_t *tried) {
    int k = 0;
    for (int i = 0; i < w->n; i++) k += wifi_names_has(seen, w->net[i].ssid) && !(tried && wifi_names_has(tried, w->net[i].ssid));
    return k;
}
void wifi_name_arg(char *out, const char *s, int len) {
    while (len > 0 && *s == ' ') { s++; len--; }
    while (len > 0 && s[len - 1] == ' ') len--;
    if (len > NET_SSID_MAX) len = NET_SSID_MAX;
    memcpy(out, s, (size_t)len); out[len] = 0;
}
void wifi_name_fit(char *out, size_t cap, const char *ssid, int max) {
    int n = (int)strlen(ssid);
    if (n <= max) { snprintf(out, cap, "%s", ssid); return; }
    int tail = (max - 3) * 2 / 5, head = max - 3 - tail;
    snprintf(out, cap, "%.*s...%s", head, ssid, ssid + n - tail);
}

/* ---- the store's calls every platform shares (update.h) ---- */
bool net_port_creds_get(char *ssid, char *pass) {
    const wifi_nets_t *w = net_port_nets();
    if (!w->n) { ssid[0] = 0; pass[0] = 0; return false; }
    snprintf(ssid, NET_SSID_MAX + 1, "%s", w->net[0].ssid); snprintf(pass, NET_PASS_MAX + 1, "%s", w->net[0].pass);
    return true;
}
int net_port_creds_count(void) { return net_port_nets()->n; }
bool net_port_creds_at(int i, char *ssid, bool *joined) {
    const wifi_nets_t *w = net_port_nets();
    if (i < 0 || i >= w->n) { ssid[0] = 0; return false; }
    snprintf(ssid, NET_SSID_MAX + 1, "%s", w->net[i].ssid); if (joined) *joined = w->net[i].joined != 0;
    return true;
}
bool net_port_creds_replaces(const char *ssid, char *victim) {
    const wifi_nets_t *w = net_port_nets(); const int v = wifi_nets_victim(w, ssid);
    if (v >= 0) snprintf(victim, NET_SSID_MAX + 1, "%s", w->net[v].ssid); else victim[0] = 0;
    return v >= 0;
}
