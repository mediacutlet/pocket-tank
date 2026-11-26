/* net_port_esp.c - the update flow's net port on the tank (common/update.h,
 * docs/OTA.md): the credential store, the Wi-Fi station, the manifest fetch
 * and the signed download into the other app slot.
 *
 * Runs only in update mode, at boot, before the tank exists (update_mode.c),
 * so the driver has the whole internal heap. Every call from the flow
 * returns at once: a worker task does the blocking work and the flow polls
 * the state each frame. net_port_off tears the whole stack down on the way
 * out - the radio is off whenever the tank is running.
 *
 *   the store     NVS namespace "wifi": nets (0.4.2: the saved networks, up
 *                 to three, ONE blob - common/wifi_nets.h has the bytes, the
 *                 order and each one's learned channel), miss (the quick
 *                 look's count), url (the director's manifest override - the
 *                 test channel), code, crew. ssid / pass / chan are 0.4.1's
 *                 one network: whenever found they are added to nets and
 *                 then erased (nets_load). Its own namespace: the tank's
 *                 reset wipes "tank", never this.
 *   the manifest  OTA_MANIFEST_BASE + the board (a GitHub Release's "latest" asset), one
 *                 per board: latest-<board>.json. Small JSON: release,
 *                 board, build, min_from, app{url,size,sha256}, note,
 *                 needs_cable, model{version,size,url}. A manifest naming
 *                 another board is refused (common/update.c)
 *   the board     every image carries a board marker right after its app
 *                 descriptor (pt_board_marker below, PT_BOARD_MARKER_OFFSET):
 *                 the download reads it back from the slot at the first
 *                 sector and stops on another board's - all three boards
 *                 share the signing key, so the signature alone would let a
 *                 bowl install the 1.8's image (2026-10-02)
 *   the download  esp_https_ota into the other slot; the image must carry
 *                 the project's signature (SECURE_SIGNED_ON_UPDATE, verified
 *                 against the running image's own key) or it is refused
 *   transport     HTTPS with the IDF certificate bundle; redirects followed
 *                 (GitHub's release assets redirect to a second host) */
#include "update.h"
#include "wifi_nets.h"
#include "net_old_keys.h"
#include "version.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "net_time.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "net";
#define WIFI_NVS_NS   "wifi"
/* the release channel: latest-<board>.json, the board this tank IS (0.3.9.3,
 * update_target_board: another board's version installed here fetches this
 * board's own) */
#define OTA_MANIFEST_BASE "https://github.com/mediacutlet/pocket-tank/releases/latest/download/latest-"
/* the Hatchery Crew check (docs/hatchery-crew.md, 0.4.1): asked before the
 * manifest; a crew board is pointed at the early builds' manifest. Any
 * answer but a clear one leaves the release channel alone. */
#define CREW_CHECK_URL    "https://pocketank.com/crew/check.php"
#define CREW_TIMEOUT_MS   6000
/* the board marker: in .rodata_custom_desc, which the linker places right
 * after esp_app_desc_t - a fixed offset in the image (PT_BOARD_MARKER_OFFSET;
 * tools/make_ota_manifest.py and make_installer.py read it back from the .bin) */
typedef struct { char magic[8]; char board[24]; } pt_board_marker_t;
const __attribute__((section(".rodata_custom_desc"), used)) pt_board_marker_t pt_board_marker = { PT_BOARD_MAGIC, PT_BOARD };

/* ---- the store ---- */
static bool nvs_get_str_ns(const char *key, char *out, size_t cap) {
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = cap; esp_err_t e = nvs_get_str(h, key, out, &n); nvs_close(h);
    if (e != ESP_OK) { out[0] = 0; return false; }
    return true;
}
/* THE SAVED NETWORKS (0.4.2, common/wifi_nets.h): up to three, one NVS blob "nets", read ONCE a boot (the UPDATES
 * page asks every frame) and written whole at every change - NVS replaces a blob in one step, so a power cut leaves
 * the old list or the new one. Names go to the log; a password never does.
 *
 * TWO TASKS READ IT (the glass's, and the radio's worker, which also moves a network to the front when it joins):
 * the list is NEVER CHANGED IN PLACE. There are two of them (2 x 299 bytes); a change is made on the one nobody is
 * reading, and one byte (s_cur) then makes it the list - a reader has the old one whole or the new one whole, and
 * pays nothing for it. Writers take turns (s_mx) through the change and its write to the flash. (A reader's pointer
 * is good until the change after next: nobody keeps one past the frame or the call it asked in.)
 *
 * 0.4.1'S KEYS (ssid / pass / chan), the rule in wifi_nets.h: found at a read, they are added to the list, the list
 * is written, and then they are erased - in that order, so a cut loses nothing and repeats harmlessly. A tank
 * rolled back to 0.4.1 BY HAND has no network; one saved there joins the list on the way back up. Every later write
 * erases them again (nets_flash) and FORGET ALL erases them before the list, so nothing forgotten comes back.
 * ONE EXCEPTION (0.4.2 audit r2, PW2-1; net_old_keys.h decides it): while this image still waits for its
 * confirmation after an update (any reset rolls back to 0.4.1, which reads only the old keys), the tank's OWN
 * writes - the move itself, a join's move to the front - leave the old keys be. They go at the next write once the
 * image is confirmed, at the latest the next boot's read (it finds them, adds them again, writes). A keeper's
 * SAVE or FORGET erases them even then. */
static wifi_nets_t s_nb[2]; static volatile uint8_t s_cur; static bool s_nets_ok;
static StaticSemaphore_t s_mx_buf; static SemaphoreHandle_t s_mx;
static void nets_lock(void) { if (!s_mx) s_mx = xSemaphoreCreateMutexStatic(&s_mx_buf); xSemaphoreTake(s_mx, portMAX_DELAY); }
static void nets_unlock(void) { xSemaphoreGive(s_mx); }
static bool image_pending(void) {                           /* asked of the partition itself: main.c's s_ota_pending is not set yet at the boot's first read */
    esp_ota_img_states_t st; const esp_partition_t *run = esp_ota_get_running_partition();
    return run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
}
static bool nets_flash(const wifi_nets_t *w, bool keys_out) {   /* (the lock held) the list, then 0.4.1's keys out (keys_out: net_old_keys.h) */
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "nets", w, sizeof *w) == ESP_OK && nvs_commit(h) == ESP_OK;
    if (ok && keys_out) { nvs_erase_key(h, "ssid"); nvs_erase_key(h, "pass"); nvs_erase_key(h, "chan"); nvs_commit(h); }
    nvs_close(h);
    return ok;
}
static void nets_load(void) {                               /* (the lock held) from the flash, into the list nobody reads, then that is the list */
    if (s_nets_ok) return;
    wifi_nets_t *w = &s_nb[s_cur ^ 1];
    nvs_handle_t h; esp_err_t e = nvs_open(WIFI_NVS_NS, NVS_READONLY, &h);
    if (e != ESP_OK) {                                       /* no namespace yet = none saved; anything else: empty for now, asked again */
        wifi_nets_clear(w); s_cur ^= 1; s_nets_ok = e == ESP_ERR_NVS_NOT_FOUND; return;
    }
    size_t n = sizeof *w;
    if (nvs_get_blob(h, "nets", w, &n) != ESP_OK || !wifi_nets_from_bytes(w, w, n)) wifi_nets_clear(w);
    char ssid[NET_SSID_MAX + 1] = "", pass[NET_PASS_MAX + 1] = ""; uint8_t chan = 0; size_t sn = sizeof ssid, pn = sizeof pass;
    bool old = nvs_get_str(h, "ssid", ssid, &sn) == ESP_OK && ssid[0];
    if (old) { if (nvs_get_str(h, "pass", pass, &pn) != ESP_OK) pass[0] = 0; nvs_get_u8(h, "chan", &chan); }
    nvs_close(h);
    if (old) old = wifi_nets_take_old(w, ssid, pass, chan);
    memset(pass, 0, sizeof pass);
    s_cur ^= 1; s_nets_ok = true;
    if (old) {
        const bool out = net_old_keys_erase_now(NET_KEYS_MOVED, image_pending());
        bool kept = nets_flash(w, out);
        ESP_LOGI(TAG, "0.4.1's saved network %s is in the list of %d now (%s)", ssid, w->n, !kept ? "NOT written - the old keys stay, read again at the next boot"
                 : out ? "written, the old keys erased" : "written; the old keys stay until this update is confirmed");
    }
}
static const wifi_nets_t *nets_cache(void) {
    if (!s_nets_ok) { nets_lock(); nets_load(); nets_unlock(); }
    return &s_nb[s_cur];
}
const wifi_nets_t *net_port_nets(void) { return nets_cache(); }
/* ONE CHANGE to the list, on the copy nobody reads; if it changed anything the copy becomes the list and goes to the
   flash. true = changed and written (a write that failed: the list is read back from the flash at the next ask).
   chan: ED_ADD -1 = the radio is not on it now (else it is, on that channel, 0 = not known) */
enum { ED_ADD, ED_JOINED, ED_FORGET };
static bool nets_edit(int op, const char *ssid, const char *pass, int chan, char *gone) {
    nets_lock(); nets_load();
    wifi_nets_t *w = &s_nb[s_cur ^ 1]; *w = s_nb[s_cur];
    bool ch = op == ED_ADD ? wifi_nets_add(w, ssid, pass, gone) : op == ED_JOINED ? wifi_nets_joined(w, ssid, chan) : wifi_nets_forget_one(w, ssid);
    if (ch && op == ED_ADD && chan >= 0) wifi_nets_joined(w, ssid, chan);
    bool ok = false;
    const net_keys_why_t why = op == ED_ADD ? NET_KEYS_SAVED : op == ED_JOINED ? NET_KEYS_JOINED : NET_KEYS_FORGOT;
    if (ch) { s_cur ^= 1; ok = nets_flash(w, net_old_keys_erase_now(why, image_pending())); if (!ok) s_nets_ok = false; }
    nets_unlock();
    return ch && ok;
}
static void miss_put(int v);
static bool on_network(const char *ssid, int *chan);        /* the radio is on it now (the wizard and the installer save after they connect) */
bool net_port_creds_set(const char *ssid, const char *pass) {
    char gone[NET_SSID_MAX + 1] = ""; int chan = -1;
    const bool had = wifi_nets_find(nets_cache(), ssid) >= 0;
    if (!on_network(ssid, &chan)) chan = -1;
    bool ok = nets_edit(ED_ADD, ssid, pass, chan, gone);
    miss_put(0);                                            /* the boot's time check looks in full again (0.4.1, the quick look) */
    ESP_LOGI(TAG, "network %s: %s (%s), %d of %d%s%s%s", !ok ? "NOT saved" : had ? "saved again" : "saved", ssid, pass && pass[0] ? "with a password" : "open",
             nets_cache()->n, WIFI_NETS_MAX, ok && gone[0] ? " - it REPLACED " : "", ok ? gone : "", ok && gone[0] ? ", the one used longest ago" : "");
    return ok;
}
bool net_port_creds_forget_one(const char *ssid) {
    if (wifi_nets_find(nets_cache(), ssid) < 0) return false;
    bool ok = nets_edit(ED_FORGET, ssid, NULL, 0, NULL);
    miss_put(0);
    ESP_LOGI(TAG, "network %s: %s, %d of %d left", ok ? "forgotten" : "NOT forgotten", ssid, nets_cache()->n, WIFI_NETS_MAX);
    return ok;
}
static bool gone_ok(esp_err_t e) { return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND; }
bool net_port_creds_forget(void) {                          /* EVERY saved network. false = the store would not open or take it
                                                               (ERASE EVERYTHING's ticked box is then owed, and tried again) */
    nets_lock();
    nvs_handle_t h; bool ok = true;
    esp_err_t o = nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h);
    if (o == ESP_OK) {
        ok = gone_ok(nvs_erase_key(h, "ssid")) & gone_ok(nvs_erase_key(h, "pass"));
        nvs_erase_key(h, "chan"); ok = nvs_commit(h) == ESP_OK && ok;   /* 0.4.1's first (see above) */
        ok = gone_ok(nvs_erase_key(h, "nets")) && ok;
        nvs_erase_key(h, "miss"); ok = nvs_commit(h) == ESP_OK && ok; nvs_close(h);
    } else if (o != ESP_ERR_NVS_NOT_FOUND) ok = false;      /* (no namespace: never a network) */
    s_nets_ok = false; nets_load();                         /* (what the flash holds now: nothing, unless it would not open) */
    nets_unlock();
    ok = ok && nets_cache()->n == 0;
    if (ok) ESP_LOGI(TAG, "every saved network forgotten"); else ESP_LOGW(TAG, "the networks could not all be forgotten (the store refused; %d left)", nets_cache()->n);
    return ok;
}
const char *net_port_manifest_url(void) {                   /* the director's override, else the release channel */
    static char url[NET_URL_MAX + 1];
    if (nvs_get_str_ns("url", url, sizeof url) && url[0]) return url;
#ifdef OTA_MANIFEST_URL
    return OTA_MANIFEST_URL;
#else
    snprintf(url, sizeof url, OTA_MANIFEST_BASE "%s.json", update_target_board());
    return url;
#endif
}
bool net_port_set_manifest_url(const char *url) {           /* NULL / "" = back to the default */
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = url && url[0] ? nvs_set_str(h, "url", url) == ESP_OK : (nvs_erase_key(h, "url"), true);
    ok = nvs_commit(h) == ESP_OK && ok; nvs_close(h);
    return ok;
}

/* ---- the Hatchery Crew: the board code and the last answer (update.h) ----
 * Made once, kept in the Wi-Fi namespace (never the save: a RESET keeps it).
 * The RNG is not guaranteed random before the radio has run, and the code is
 * made long before that (the UPDATES page shows it), so the MAC goes into the
 * hash too: every board's code is its own, and the hash keeps the MAC from
 * being read back out of it.
 * Read or made ONCE a boot (the UPDATES page asks every frame): a code the
 * flash would not keep is tried once, never shown - given to Turbo it would be
 * gone at the next boot - and the next boot tries again (0.4.1 audit F2: a
 * refused write was a new code, a SHA-256 and an NVS write every frame). */
static char s_code[NET_CODE_LEN + 1];
static int8_t s_code_st;                                    /* 0 not yet, 1 read or kept, -1 made and NOT kept */
bool net_port_board_code(char out[NET_CODE_LEN + 1]) {
    if (s_code_st) { if (s_code_st < 0) return false; memcpy(out, s_code, sizeof s_code); return true; }
    if (nvs_get_str_ns("code", out, NET_CODE_LEN + 1) && strlen(out) == NET_CODE_LEN) { memcpy(s_code, out, sizeof s_code); s_code_st = 1; return true; }
    uint8_t seed[6 + 32 + 8] = { 0 }, h[32];
    esp_read_mac(seed, ESP_MAC_WIFI_STA);
    esp_fill_random(seed + 6, 32);
    int64_t t = esp_timer_get_time(); memcpy(seed + 38, &t, sizeof t);
    mbedtls_sha256(seed, sizeof seed, h, 0);
    static const char A[] = NET_CODE_ALPHABET;
    for (int i = 0, k = 0; i < NET_CODE_LEN; i++) out[i] = i == 4 ? '-' : A[h[k++] % (sizeof A - 1)];
    out[NET_CODE_LEN] = 0;
    nvs_handle_t nh; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &nh) != ESP_OK) { s_code_st = -1; return false; }
    bool ok = nvs_set_str(nh, "code", out) == ESP_OK && nvs_commit(nh) == ESP_OK; nvs_close(nh);
    ESP_LOGI(TAG, "board code made (%s)", ok ? "kept" : "NOT kept - not shown, tried again at the next boot");
    if (ok) memcpy(s_code, out, sizeof s_code);
    s_code_st = ok ? 1 : -1;
    return ok;
}
int net_port_crew(void) {
    nvs_handle_t h; uint8_t v = 0;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    if (nvs_get_u8(h, "crew", &v) != ESP_OK) v = 0;
    nvs_close(h); return v == 1;
}
static void crew_set(int on) {
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "crew", on ? 1 : 0); nvs_commit(h); nvs_close(h);
}

/* ---- the radio ---- */
enum { OP_NONE, OP_SCAN, OP_CONNECT, OP_CHECK, OP_INSTALL };
static volatile int  s_op, s_state, s_err, s_percent;
static volatile bool s_abort;
static TaskHandle_t  s_task;
static EventGroupHandle_t s_ev;
#define EV_GOT_IP       (1 << 0)
#define EV_DISCONNECTED (1 << 1)
static esp_netif_t  *s_netif;
static bool          s_up, s_connected;
static int           s_disc_reason;
static char          s_ssid[NET_SSID_MAX + 1], s_pass[NET_PASS_MAX + 1];
static net_ap_t      s_aps[NET_SCAN_MAX]; static int s_n_aps;
static wifi_names_t  s_saw;                                 /* the saved networks the last scan heard, of ALL it heard - BY NAME (wifi_nets.h: the list reorders) */
static uint8_t       s_saw_chan[WIFI_NETS_MAX];             /* ... and the channel each answered on (the first of its records), beside its name */
static bool on_network(const char *ssid, int *chan) {
    if (!s_up || !s_connected || strncmp(s_ssid, ssid, NET_SSID_MAX)) return false;
    wifi_ap_record_t ap; *chan = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.primary : 0;
    return true;
}
static update_manifest_t s_m; static char s_app_url[NET_URL_MAX + 1];
static esp_https_ota_handle_t s_ota;

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        s_disc_reason = d->reason; s_connected = false;
        xEventGroupSetBits(s_ev, EV_DISCONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_ev, EV_GOT_IP);
    }
}
static bool radio_up(void) {
    if (s_up) return true;
    ESP_LOGI(TAG, "radio up: internal heap %u KB, psram %u KB free before", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    if (esp_netif_init() != ESP_OK) return false;
    if (esp_event_loop_create_default() != ESP_OK) return false;
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_wifi_start() != ESP_OK) return false;
    s_up = true;
    ESP_LOGI(TAG, "radio up: internal heap %u KB free after", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    return true;
}

/* one scan, blocking (about two seconds): the list for the page (the strongest NET_SCAN_MAX names), and which of
   the SAVED networks answered - asked of EVERY record the driver kept, one at a time (esp_wifi_scan_get_ap_record:
   no table of them is held; until this review the strongest 40 were copied out and the rest dropped, so a weak
   saved network in a crowded building was "not in range"). A hidden network is never in a scan: it is joined only
   while it is the one used last. false = the radio would not scan */
static bool scan_once(void) {
    wifi_scan_config_t sc = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    wifi_names_clear(&s_saw); memset(s_saw_chan, 0, sizeof s_saw_chan);
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return false;
    const wifi_nets_t *w = nets_cache();
    wifi_ap_record_t rec; int heard = 0;
    s_n_aps = 0;
    while (esp_wifi_scan_get_ap_record(&rec) == ESP_OK) {    /* (each call hands one record over and frees it) */
        char name[NET_SSID_MAX + 1]; snprintf(name, sizeof name, "%.*s", NET_SSID_MAX, (const char *)rec.ssid);   /* (a 32-byte name has no 0 of its own) */
        heard++;
        if (!name[0]) continue;                              /* no hidden ones */
        { const int k = wifi_nets_heard(w, &s_saw, name); if (k >= 0) s_saw_chan[k] = rec.primary; }
        int at = -1;                                         /* the page's list: names unique, strongest first whatever order the records come in */
        for (int k = 0; k < s_n_aps; k++) if (!strcmp(s_aps[k].ssid, name)) { at = k; break; }
        if (at >= 0) { if (rec.rssi <= s_aps[at].rssi) continue; }
        else if (s_n_aps < NET_SCAN_MAX) at = s_n_aps++;
        else if (rec.rssi > s_aps[NET_SCAN_MAX - 1].rssi) at = NET_SCAN_MAX - 1;
        else continue;
        net_ap_t ap = { .rssi = rec.rssi, .secured = rec.authmode != WIFI_AUTH_OPEN }; snprintf(ap.ssid, sizeof ap.ssid, "%s", name);
        for (; at > 0 && s_aps[at - 1].rssi < ap.rssi; at--) s_aps[at] = s_aps[at - 1];
        s_aps[at] = ap;
    }
    esp_wifi_clear_ap_list();
    ESP_LOGI(TAG, "scan: %d networks (%d shown), %d of the %d saved in range", heard, s_n_aps, s_saw.n, w->n);
    return true;
}
static void do_scan(void) { if (scan_once()) s_state = NET_DONE; else { s_err = NET_ERR_RADIO; s_state = NET_FAILED; } }
int net_port_scan_saved(const wifi_names_t *tried, char *ssid, char *pass) {   /* the list as it is NOW against the names the scan heard */
    const wifi_nets_t *w = nets_cache(); const int i = wifi_nets_next(w, &s_saw, tried);
    if (i >= 0) { snprintf(ssid, NET_SSID_MAX + 1, "%s", w->net[i].ssid); snprintf(pass, NET_PASS_MAX + 1, "%s", w->net[i].pass); }
    return i;
}
static int saw_chan(const char *ssid) {                      /* the channel the scan heard it on, 0 = not heard */
    for (int k = 0; k < s_saw.n; k++) if (!strncmp(s_saw.ssid[k], ssid, NET_SSID_MAX)) return s_saw_chan[k];
    return 0;
}
/* the channel the network being joined last answered on (0 = not known): the boot's
 * first try goes straight there instead of sweeping all thirteen (0.3.9). Each saved
 * network has its own (0.4.2): net_time_sync sets this beside s_ssid */
static int s_boot_chan;
static int chan_get(void) { return s_boot_chan; }
/* THE QUICK LOOK (0.4.1): "miss" = the boot's time checks in a row that never saw the router (net_time_sync,
   main.c clock_gate.h). Any connect clears it - the boot's, the updates page's, the Wi-Fi setup's - and so does
   saving a network (net_port_creds_set) */
static int miss_get(void) {
    nvs_handle_t h; uint8_t v = 0;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "miss", &v); nvs_close(h); }
    return v;
}
static void miss_put(int v) {
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (v > 255) v = 252;                                     /* (on round: 252 looks in full, 253-255 quick - clock_gate.h CLOCK_FULL_EVERY) */
    if (v) nvs_set_u8(h, "miss", (uint8_t)v); else nvs_erase_key(h, "miss");
    nvs_commit(h); nvs_close(h);
}
int net_time_missed(void) { return miss_get(); }
static void chan_learn(void) {                               /* written only when it moved: the boot connects every morning */
    if (miss_get()) miss_put(0);                             /* (the router is there: the next time check looks in full) */
    wifi_ap_record_t ap; const int c = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.primary : 0;
    /* a SAVED one joined (0.4.2): it is the one tried first from now on, with its channel. Nothing is written when it
       was first already and its channel stands - one saved network writes as rarely as 0.4.1 did */
    if (nets_edit(ED_JOINED, s_ssid, NULL, c, NULL)) ESP_LOGI(TAG, "%s answers on channel %d: tried first from now on", s_ssid, c);
}
static int s_tries, s_last_reason; static bool s_last_timed_out;   /* the last connect, for net_time_why */
static bool s_seen;                                          /* the last connect saw the router: some try ended other than "not found" */
static void sta_config(int chan) {                           /* chan 0: sweep every channel, the strongest answer wins */
    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, s_ssid, sizeof wc.sta.ssid);             /* 32 bytes, NUL-padded or full (the driver takes both) */
    memcpy(wc.sta.password, s_pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true; wc.sta.pmf_cfg.required = false;
    wc.sta.channel = chan;                                       /* fast scan from there: the first answer on it is taken (a mesh's nearest node), */
    wc.sta.scan_method = chan ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;   /* and a router that moved is still found further on */
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
}
/* boot: the morning time check (net_time_sync). A router not seen is tried
 * again rather than given up on (a cold radio's first sweep misses one now
 * and then - Bananna's pendant, 2026-10-23, two mornings running), the first
 * try uses the learned channel, and deadline_us bounds the whole thing. A
 * router still not seen after the tries is swept for until the deadline: an
 * empty sweep ends a try in ~2-4 s, so three were over in 12-15 s (Strato's
 * stopwatch, 2026-10-23) and the 30 s went unused. The page keeps its quick
 * NO SIGNAL. */
/* quick (0.4.1, a chip-board that owes time and did not see its router at the last boot: clock_gate.h): `quick`
 * tries while the router is still not seen, then the time check gives up; a router seen in them gets the
 * whole budget, tries and sweeps as ever. */
static void do_connect_n(int attempts, int wait_ms, int64_t deadline_us, bool boot, int quick) {
    int chan = boot ? chan_get() : 0;
    sta_config(chan);
    int err = NET_ERR_NO_SIGNAL;
    s_tries = 0; s_last_reason = 0; s_last_timed_out = false; s_seen = false;
    for (int attempt = 0; !s_abort; attempt++) {                  /* routers refuse first tries; the page's TRY AGAIN does five more */
        if (quick && !s_seen && attempt >= quick) break;          /* the quick look: still nobody there */
        if (attempt >= attempts && !(boot && deadline_us && s_last_reason == WIFI_REASON_NO_AP_FOUND)) break;
        int wait = wait_ms;
        if (deadline_us) {
            int64_t left_ms = (deadline_us - esp_timer_get_time()) / 1000;
            if (left_ms < 2000) break;                           /* not worth a try: the glass has waited long enough */
            if (left_ms < wait) wait = (int)left_ms;
        }
        xEventGroupClearBits(s_ev, EV_GOT_IP | EV_DISCONNECTED);
        s_disc_reason = 0; s_tries = attempt + 1;
        if (esp_wifi_connect() != ESP_OK) { err = NET_ERR_RADIO; break; }
        EventBits_t b = xEventGroupWaitBits(s_ev, EV_GOT_IP | EV_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(wait));
        if (b & EV_GOT_IP) { ESP_LOGI(TAG, "connected to %s (attempt %d)", s_ssid, attempt + 1); s_seen = true; chan_learn(); s_state = NET_DONE; return; }
        int r = s_disc_reason;
        s_last_reason = r; s_last_timed_out = !(b & EV_DISCONNECTED);
        if (r != WIFI_REASON_NO_AP_FOUND) s_seen = true;          /* refused, silent, dropped: the router is there (a radio that would not start is not) */
        ESP_LOGW(TAG, "connect attempt %d%s: %s (reason %d)", attempt + 1, chan ? " (learned channel)" : "", b & EV_DISCONNECTED ? "disconnected" : "timed out", r);
        if (r == WIFI_REASON_NO_AP_FOUND && !boot) { err = NET_ERR_NO_SIGNAL; break; }
        if (r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_HANDSHAKE_TIMEOUT
            || r == WIFI_REASON_AUTH_EXPIRE || r == WIFI_REASON_ASSOC_EXPIRE || r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY
            || r == WIFI_REASON_CONNECTION_FAIL) err = NET_ERR_PASSWORD;
        else err = NET_ERR_NO_SIGNAL;
        if (chan) { esp_wifi_disconnect(); chan = 0; sta_config(0); }   /* the learned channel missed: sweep them all from here */
        int pause = 1000 + (attempt < attempts ? attempt : attempts - 1) * 500;   /* the sweeps past the tries keep the last pause */
        if (deadline_us && (deadline_us - esp_timer_get_time()) / 1000 < pause + 2000) break;
        vTaskDelay(pdMS_TO_TICKS(pause));
    }
    esp_wifi_disconnect();
    s_err = s_abort ? NET_ERR_ABORTED : err; s_state = NET_FAILED;
}
static int s_attempts = 5, s_wait_ms = 15000;
static void do_connect(void) { do_connect_n(s_attempts, s_wait_ms, 0, false, 0); }
/* provisioning at install (update_mode.c): the page waits 45 s for an answer */
void net_port_esp_connect_budget(int attempts, int wait_ms) { s_attempts = attempts; s_wait_ms = wait_ms; }

/* ---- the time (net_time.h, 2026-10-03) ----
 * Plain SNTP by hand: ESP-IDF's client waits a random 0-5 s before its first
 * question (LWIP_SNTP_STARTUP_DELAY) and sets the system clock itself - the
 * boot wants neither (the keeper is looking at a dark glass, and the first
 * sync must not move the clock before the save is loaded). One 48-byte
 * question, the server's transmit time back; three servers, 2 s each. */
static bool ntp_ask(const char *host, int64_t *unix_out) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM }, *res = NULL;
    if (getaddrinfo(host, "123", &hints, &res) != 0 || !res) { ESP_LOGW(TAG, "time: %s did not resolve", host); return false; }
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool ok = false;
    if (s >= 0) {
        struct timeval tv = { .tv_sec = 2 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        uint8_t q[48] = { 0x23 }, r[48];                         /* LI 0, version 4, mode 3 = a client */
        if (sendto(s, q, sizeof q, 0, res->ai_addr, res->ai_addrlen) == (int)sizeof q
            && recv(s, r, sizeof r, 0) == (int)sizeof r && (r[0] & 7) == 4 && r[1] != 0) {   /* mode 4 = a server's answer, stratum 0 = a refusal */
            uint32_t secs = (uint32_t)r[40] << 24 | (uint32_t)r[41] << 16 | (uint32_t)r[42] << 8 | r[43];   /* the transmit stamp, seconds since 1900 */
            int64_t u = (int64_t)secs - 2208988800LL;
            if (u > 1700000000) { *unix_out = u; ok = true; }
        }
        close(s);
    }
    freeaddrinfo(res);
    if (!ok) ESP_LOGW(TAG, "time: no answer from %s", host);
    return ok;
}
/* A dark glass is waiting: three tries of 10 s, all of it inside 30 s
 * (Strato, 2026-10-23; it was two of 8 s, and one when the first sweep
 * missed the router); a router not seen is swept for until the 30 s are
 * up (do_connect_n). The page's five stay the page's. */
#define SYNC_TRIES    3
#define SYNC_WAIT_MS  10000
#define SYNC_BUDGET_US 30000000LL
#define SYNC_QUICK_TRIES 2                                     /* the quick look (0.4.1): the learned channel, then one sweep of them all */
#define SYNC_LOOK_MIN_MS   4000                                /* several saved: a scan (about 2 s) is not begun with less than this left */
#define SYNC_LOOK_PAUSE_MS 1500                                /* ... and the pause between two of them (the sweeps' own) */
static char s_why[80] = "no sync yet";
const char *net_time_why(void) { return s_why; }
static void why_connect_failed(void) {
    const char *what = s_err == NET_ERR_RADIO ? "the radio would not start"
                     : s_last_reason == WIFI_REASON_NO_AP_FOUND ? "the router was not seen"
                     : s_err == NET_ERR_PASSWORD ? "the router refused (password?)"
                     : s_last_timed_out ? "no answer from the router" : "the router dropped it";
    snprintf(s_why, sizeof s_why, "%s, %d %s, reason %d", what, s_tries, s_tries == 1 ? "try" : "tries", s_last_reason);
}
static void use_net(const wifi_net_t *e) { snprintf(s_ssid, sizeof s_ssid, "%s", e->ssid); snprintf(s_pass, sizeof s_pass, "%s", e->pass); s_boot_chan = e->chan; }
/* THE TIME CHECK'S CONNECT (0.4.2, several saved networks; wifi_nets.h has the order and wifi_nets_next the
 * "which one next" - the host tests that; this is the radio's side of it).
 *  - ONE saved: 0.4.1 to the letter.
 *  - SEVERAL: the one used last gets the first try as ever - its learned channel, and the driver's sweep on from
 *    there - so the morning at home is not a millisecond slower. Seen in that try (refused, silent), it keeps its
 *    tries as ever. If it did not join, the radio SCANS (about 2 s) and the saved networks IN RANGE are tried in the
 *    tank's order, each ONCE: one that is in range and will not join (its password changed, a neighbor's network
 *    of the same name) is passed over for the next, and what is left of the budget is shared between the ones
 *    still to try (the last gets all of it). The one used last is among them again only if its first try did not
 *    see it (a cold radio's first sweep misses a router now and then).
 *    The quick look: the first try, ONE scan, one try at each in range, then it gives up.
 *    The full look: more scans, SYNC_LOOK_PAUSE_MS apart, while SYNC_LOOK_MIN_MS of the budget is left - it ends
 *    inside SYNC_BUDGET_US, as the sweeps for one network did, and never later.
 *  - "SEEN", for the quick-look rule (net_time_sync: not seen -> miss + 1 -> the next boot looks quick), is the FIRST
 *    try's alone, as it was with one network. A name the scan heard that would not join does NOT count: a tank with
 *    nothing it can join stays on the quick look instead of spending the whole budget at every wake.
 * s_looks: the scans made; s_cands: the networks tried after them (net_time_why) */
static int s_looks, s_cands;
static void sync_connect(int64_t deadline_us, bool quick) {
    const int n = nets_cache()->n;
    use_net(&nets_cache()->net[0]); s_looks = 0; s_cands = 0;
    if (n <= 1) { do_connect_n(SYNC_TRIES, SYNC_WAIT_MS, deadline_us, true, quick ? SYNC_QUICK_TRIES : 0); return; }
    do_connect_n(SYNC_TRIES, SYNC_WAIT_MS, deadline_us, true, 1);
    if (s_state == NET_DONE || s_err == NET_ERR_RADIO || s_abort) return;
    const bool first_seen = s_seen;
    wifi_names_t tried; wifi_names_clear(&tried);
    if (first_seen) wifi_names_add(&tried, s_ssid);          /* (it was there and had its tries) */
    bool looked = false;
    while (!s_abort && (deadline_us - esp_timer_get_time()) / 1000 >= SYNC_LOOK_MIN_MS) {
        if (!looked) { s_looks++; looked = true; if (!scan_once()) wifi_names_clear(&s_saw); }
        const wifi_nets_t *w = nets_cache();
        const int i = wifi_nets_next(w, &s_saw, &tried), left = wifi_nets_left(w, &s_saw, &tried);
        if (i < 0) {                                         /* nobody (else) in range */
            if (quick || (deadline_us - esp_timer_get_time()) / 1000 < SYNC_LOOK_PAUSE_MS + SYNC_LOOK_MIN_MS) break;
            vTaskDelay(pdMS_TO_TICKS(SYNC_LOOK_PAUSE_MS)); looked = false;
            continue;
        }
        use_net(&w->net[i]); if (saw_chan(s_ssid)) s_boot_chan = saw_chan(s_ssid);
        wifi_names_add(&tried, s_ssid); s_cands++;
        const int64_t now = esp_timer_get_time(), share = now + (deadline_us - now) / (left > 0 ? left : 1);
        ESP_LOGI(TAG, "time: %s is in range (scan %d, channel %d), %d ms for it", s_ssid, s_looks, s_boot_chan, (int)((share - now) / 1000));
        s_state = NET_BUSY;
        do_connect_n(quick ? 1 : SYNC_TRIES, SYNC_WAIT_MS, share, true, quick ? 1 : 0);
        if (s_state == NET_DONE || s_err == NET_ERR_RADIO) return;
    }
    if (!s_cands && !first_seen) { s_last_reason = WIFI_REASON_NO_AP_FOUND; s_last_timed_out = false; s_err = NET_ERR_NO_SIGNAL; }
    if (s_abort) s_err = NET_ERR_ABORTED;
    s_seen = first_seen; s_state = NET_FAILED;
}
bool net_time_sync(int64_t *unix_out, int64_t *at_us, bool quick) {
    if (!nets_cache()->n) { snprintf(s_why, sizeof s_why, "no network saved"); return false; }
    int64_t t0 = esp_timer_get_time(), deadline = t0 + SYNC_BUDGET_US;
    if (!s_ev) s_ev = xEventGroupCreate();
    s_abort = false; s_state = NET_BUSY;
    bool ok = false;
    if (!radio_up()) snprintf(s_why, sizeof s_why, "the radio would not start");
    else {
        sync_connect(deadline, quick);
        if (s_state != NET_DONE) {
            why_connect_failed();
            if (s_looks && s_err != NET_ERR_RADIO)
                snprintf(s_why, sizeof s_why, "none of the %d saved joined: %d %s, %d in range tried, reason %d", nets_cache()->n, s_looks, s_looks == 1 ? "scan" : "scans", s_cands, s_last_reason);
            if (s_err != NET_ERR_RADIO && !s_seen) miss_put(miss_get() + 1);   /* nobody there: the next boot's check is a quick look */
            else if (s_seen && miss_get()) miss_put(0);                          /* seen, just not answering: the next one looks in full */
            if (quick && !s_seen && s_err != NET_ERR_RADIO) { size_t n = strlen(s_why); snprintf(s_why + n, sizeof s_why - n, " (a quick look)"); }
        }
        else {
            static const char *HOSTS[] = { "pool.ntp.org", "time.google.com", "time.cloudflare.com" };
            for (size_t i = 0; i < sizeof HOSTS / sizeof *HOSTS && !ok; i++) {
                if (i && esp_timer_get_time() > deadline) break;  /* the first server is always asked */
                if (ntp_ask(HOSTS[i], unix_out)) { *at_us = esp_timer_get_time(); ok = true; }
            }
            if (ok) snprintf(s_why, sizeof s_why, "on the network at try %d", s_tries);
            else snprintf(s_why, sizeof s_why, "on the network (try %d), but no time server answered", s_tries);
        }
    }
    net_port_off();
    ESP_LOGI(TAG, "time from the internet: %s in %d ms (%s)", ok ? "got it" : "FAILED", (int)((esp_timer_get_time() - t0) / 1000), s_why);
    return ok;
}
static uint32_t parse_release(const char *s) {
    int a = 0, b = 0, c = 0, d = 0; if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) < 2) return 0;
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;   /* version.h: four bytes since 0.3.8.1 */
}
static bool parse_manifest(const char *json, update_manifest_t *m) {
    cJSON *root = cJSON_Parse(json); if (!root) return false;
    memset(m, 0, sizeof *m); s_app_url[0] = 0;
    const cJSON *rel = cJSON_GetObjectItem(root, "release"), *app = cJSON_GetObjectItem(root, "app"), *model = cJSON_GetObjectItem(root, "model");
    if (!cJSON_IsString(rel) || !cJSON_IsObject(app)) { cJSON_Delete(root); return false; }
    snprintf(m->release, sizeof m->release, "%s", rel->valuestring);
    const cJSON *num = cJSON_GetObjectItem(root, "release_num");
    m->release_num = cJSON_IsNumber(num) ? (uint32_t)num->valuedouble : parse_release(m->release);
    const cJSON *v;
    if (cJSON_IsString(v = cJSON_GetObjectItem(root, "build"))) snprintf(m->build, sizeof m->build, "%s", v->valuestring);
    if (cJSON_IsString(v = cJSON_GetObjectItem(root, "board"))) snprintf(m->board, sizeof m->board, "%s", v->valuestring);   /* none = refused */
    if (cJSON_IsString(v = cJSON_GetObjectItem(root, "min_from"))) m->min_from = parse_release(v->valuestring);
    else if (cJSON_IsNumber(v)) m->min_from = (uint32_t)v->valuedouble;
    if (cJSON_IsString(v = cJSON_GetObjectItem(root, "note"))) snprintf(m->note, sizeof m->note, "%s", v->valuestring);
    if (cJSON_IsBool(v = cJSON_GetObjectItem(root, "needs_cable"))) m->needs_cable = cJSON_IsTrue(v);
    if (cJSON_IsString(v = cJSON_GetObjectItem(app, "url"))) snprintf(s_app_url, sizeof s_app_url, "%s", v->valuestring);
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(app, "size"))) m->app_size = (uint32_t)v->valuedouble;
    if (cJSON_IsObject(model)) {
        if (cJSON_IsString(v = cJSON_GetObjectItem(model, "version"))) snprintf(m->model_version, sizeof m->model_version, "%s", v->valuestring);
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(model, "size"))) m->model_size = (uint32_t)v->valuedouble;
        m->model_offered = cJSON_IsString(cJSON_GetObjectItem(model, "url"));
        /* phase one cannot fetch a model: a release that needs a different one takes the cable */
        if (m->model_version[0] && strcmp(m->model_version, PT_MODEL_TAG) != 0) m->needs_cable = true;
    }
    cJSON_Delete(root);
    return s_app_url[0] != 0 && m->release_num != 0;
}
/* one small GET into buf (cap - 1 bytes and a 0): the HTTP status, 0 = no
 * answer. GitHub answers a release asset's address with a redirect, twice
 * (releases/latest/download -> releases/download/<tag> -> the asset host, a
 * 900-byte signed address). The open / fetch_headers pair used here does NOT
 * follow redirects by itself (only esp_http_client_perform does): 0.3.0's
 * first build read the 302 as the answer and said NO ANSWER to every check
 * against the real release - a tank that could never see an update
 * (2026-10-04; the rehearsal's files sat on a host that answers directly).
 * Follow them by hand, a few hops at most. */
static int http_get_small(const char *url, char *buf, int cap, int timeout_ms, int *len) {
    *len = 0; buf[0] = 0;
    esp_http_client_config_t hc = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = timeout_ms,
                                    .buffer_size = 2048, .buffer_size_tx = 2048, .max_redirection_count = 8 };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (!c) return 0;
    int status = 0;
    for (int hop = 0; hop < 6; hop++) {
        if (esp_http_client_open(c, 0) != ESP_OK) { status = 0; break; }
        (void)esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) break;
        ESP_LOGI(TAG, "HTTP %d, following the redirect (hop %d)", status, hop + 1);
        if (esp_http_client_set_redirection(c) != ESP_OK) break;
        int drained = 0; esp_http_client_flush_response(c, &drained);
        esp_http_client_close(c);
    }
    int n = 0;
    if (status) { while (n < cap - 1) { int r = esp_http_client_read(c, buf + n, cap - 1 - n); if (r <= 0) break; n += r; } }
    buf[n] = 0; *len = n;
    esp_http_client_close(c); esp_http_client_cleanup(c);
    return status;
}

/* the crew check: the board's code hashed (the code itself never leaves the
 * board), the board it IS. 200 {"crew":true,"manifest":"https://..."} -> that
 * manifest; {"crew":true} -> crew, the release channel; 404 or "crew":false
 * -> not crew; anything else (no answer, a 5xx, a page we don't understand)
 * -> the release channel and the last answer stands. A crew outage can never
 * stop a public update. */
static void crew_check(char *manifest, size_t cap, char *buf, int bufcap) {
    manifest[0] = 0;
    char code[NET_CODE_LEN + 1];
    if (!net_port_board_code(code)) return;
    uint8_t h[32]; mbedtls_sha256((const uint8_t *)code, NET_CODE_LEN, h, 0);
    char url[NET_URL_MAX + 1], hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    snprintf(url, sizeof url, CREW_CHECK_URL "?b=%s&c=%s", update_target_board(), hex);
    int n = 0, status = http_get_small(url, buf, bufcap, CREW_TIMEOUT_MS, &n);
    if (status == 404) { ESP_LOGI(TAG, "crew: not on the list (%.8s)", hex); crew_set(0); return; }
    if (status != 200 || n <= 0) { ESP_LOGI(TAG, "crew: no clear answer (HTTP %d) - the release channel", status); return; }
    cJSON *root = cJSON_Parse(buf);
    const cJSON *crew = root ? cJSON_GetObjectItem(root, "crew") : NULL, *m = root ? cJSON_GetObjectItem(root, "manifest") : NULL;
    if (cJSON_IsBool(crew)) {
        crew_set(cJSON_IsTrue(crew));
        if (cJSON_IsTrue(crew) && cJSON_IsString(m) && !strncmp(m->valuestring, "https://", 8) && strlen(m->valuestring) <= cap - 1)
            snprintf(manifest, cap, "%s", m->valuestring);
        ESP_LOGI(TAG, "crew: %s (%.8s)%s%s", cJSON_IsTrue(crew) ? "HATCHERY CREW" : "not on the list", hex,
                 manifest[0] ? ", early builds at " : "", manifest);
    } else ESP_LOGW(TAG, "crew: an answer without \"crew\" - the release channel");
    cJSON_Delete(root);
}

static int fetch_manifest(const char *url, char *buf) {   /* the manifest at url into s_m / s_app_url: a NET_ERR_* */
    ESP_LOGI(TAG, "manifest: %s", url);
    int n = 0, status = http_get_small(url, buf, 4096, 12000, &n);
    if (status == 200 && n > 0) return parse_manifest(buf, &s_m) ? NET_ERR_NONE : NET_ERR_BAD_MANIFEST;
    ESP_LOGW(TAG, "manifest: HTTP %d, %d bytes", status, n);
    return status == 404 ? NET_ERR_BAD_MANIFEST : NET_ERR_NO_NET;
}
static void do_check(void) {
    char *buf = heap_caps_malloc(4096 + NET_URL_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { s_err = NET_ERR_NO_NET; s_state = NET_FAILED; return; }
    char *crew_manifest = buf + 4096;                       /* the early manifest's URL rides past the answer's 4 KB, in PSRAM: never
                                                               .bss (161 B of internal RAM, 0.4.1 audit F3) nor the worker's stack,
                                                               where the TLS handshake runs (an answer stops at 4095 bytes + its 0) */
    crew_manifest[0] = 0;
    char override[NET_URL_MAX + 1];
    bool test_channel = nvs_get_str_ns("url", override, sizeof override) && override[0];   /* the director's `ota url` wins */
    if (!test_channel) crew_check(crew_manifest, NET_URL_MAX + 1, buf, 4096);
    const char *url = !test_channel && crew_manifest[0] ? crew_manifest : net_port_manifest_url();
    int err = fetch_manifest(url, buf);
    if (err != NET_ERR_NONE && url == crew_manifest) {   /* the early manifest would not come (a 404, a 5xx, no answer, a page we
                                                            can't read): the release channel's, as the crew check promises - a crew
                                                            outage never stops a public update (0.4.1 audit F4) */
        ESP_LOGW(TAG, "crew: the early manifest failed - the release channel's instead");
        err = fetch_manifest(net_port_manifest_url(), buf);
    }
    free(buf);
    if (err == NET_ERR_NONE) {
        ESP_LOGI(TAG, "manifest: release %s for %s (build %s), app %lu B at %s%s", s_m.release, s_m.board[0] ? s_m.board : "NO BOARD", s_m.build,
                 (unsigned long)s_m.app_size, s_app_url, s_m.needs_cable ? " - NEEDS THE CABLE" : "");
        s_state = NET_DONE;
    } else { s_err = err; s_state = NET_FAILED; }
}
static void do_install(void) {
    esp_http_client_config_t hc = { .url = s_app_url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 20000,
                                    .buffer_size = 4096, .buffer_size_tx = 2048, .max_redirection_count = 8, .keep_alive_enable = true };
    esp_https_ota_config_t oc = { .http_config = &hc, .bulk_flash_erase = false };
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (!slot) { s_err = NET_ERR_SPACE; s_state = NET_FAILED; return; }
    if (s_m.app_size && s_m.app_size > slot->size) { ESP_LOGE(TAG, "image %lu B > slot %lu B", (unsigned long)s_m.app_size, (unsigned long)slot->size); s_err = NET_ERR_SPACE; s_state = NET_FAILED; return; }
    ESP_LOGI(TAG, "download %s -> %s (0x%lx)", s_app_url, slot->label, (unsigned long)slot->address);
    esp_err_t e = esp_https_ota_begin(&oc, &s_ota);
    if (e != ESP_OK) { ESP_LOGE(TAG, "ota begin: %s", esp_err_to_name(e)); s_err = NET_ERR_DOWNLOAD; s_state = NET_FAILED; return; }
    int total = esp_https_ota_get_image_size(s_ota);
    if (total <= 0) total = (int)s_m.app_size;
    int64_t last_log = 0;
    bool board_checked = false;
    while (!s_abort) {
        e = esp_https_ota_perform(s_ota);
        if (e != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        int got = esp_https_ota_get_image_len_read(s_ota);
        if (!board_checked && got >= 4096) {                 /* the marker is in the first sector, written by now */
            pt_board_marker_t mk; board_checked = true;
            const char *want = update_target_board();       /* the board this IS (0.3.9.3), not always this build's */
            bool ok = esp_partition_read(slot, PT_BOARD_MARKER_OFFSET, &mk, sizeof mk) == ESP_OK
                   && !memcmp(mk.magic, PT_BOARD_MAGIC, sizeof mk.magic) && !strncmp(mk.board, want, sizeof mk.board);
            if (!ok) {
                mk.board[sizeof mk.board - 1] = 0;
                ESP_LOGE(TAG, "download: the image is for board \"%s\", this is %s - refused", memcmp(mk.magic, PT_BOARD_MAGIC, 8) ? "(no marker)" : mk.board, want);
                esp_https_ota_abort(s_ota); s_ota = NULL; s_err = NET_ERR_BOARD; s_state = NET_FAILED; return;
            }
            ESP_LOGI(TAG, "download: the image is for %s - this board%s", want, strcmp(want, PT_BOARD) ? " (its own version, over the 1.8's)" : "");
        }
        s_percent = total > 0 ? (int)((int64_t)got * 99 / total) : 0;
        int64_t now = esp_timer_get_time();
        if (now - last_log > 5000000) { ESP_LOGI(TAG, "download: %d / %d bytes (%d%%)", got, total, s_percent); last_log = now; }
    }
    if (s_abort) { esp_https_ota_abort(s_ota); s_ota = NULL; s_err = NET_ERR_ABORTED; s_state = NET_FAILED; ESP_LOGW(TAG, "download aborted"); return; }
    if (e != ESP_OK || !esp_https_ota_is_complete_data_received(s_ota)) {
        ESP_LOGE(TAG, "download: %s%s", esp_err_to_name(e), e == ESP_OK ? " (incomplete)" : "");
        esp_https_ota_abort(s_ota); s_ota = NULL;
        s_err = e == ESP_ERR_OTA_VALIDATE_FAILED || e == ESP_ERR_IMAGE_INVALID ? NET_ERR_VERIFY : NET_ERR_DOWNLOAD; s_state = NET_FAILED; return;
    }
    e = esp_https_ota_finish(s_ota); s_ota = NULL;          /* verifies the image (the signature too) and arms the slot */
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ota finish: %s", esp_err_to_name(e));
        s_err = e == ESP_ERR_OTA_VALIDATE_FAILED || e == ESP_ERR_IMAGE_INVALID ? NET_ERR_VERIFY : NET_ERR_DOWNLOAD; s_state = NET_FAILED; return;
    }
    s_percent = 100;
    ESP_LOGI(TAG, "installed: %s boots next", slot->label);
    s_state = NET_DONE;
}
static void worker(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t op = 0;
        xTaskNotifyWait(0, 0xffffffff, &op, portMAX_DELAY);
        if (op == OP_NONE) continue;
        if (!radio_up()) { s_err = NET_ERR_RADIO; s_state = NET_FAILED; continue; }
        switch (op) {
        case OP_SCAN: do_scan(); break;
        case OP_CONNECT: do_connect(); break;
        case OP_CHECK: do_check(); break;
        case OP_INSTALL: do_install(); break;
        default: break;
        }
    }
}
static void begin(int op) {
    if (!s_ev) s_ev = xEventGroupCreate();
    if (!s_task) xTaskCreatePinnedToCore(worker, "net", 12288, NULL, 5, &s_task, 1);
    s_op = op; s_state = NET_BUSY; s_err = NET_ERR_NONE; s_abort = false;
    xTaskNotify(s_task, (uint32_t)op, eSetValueWithOverwrite);
}
void net_port_scan_start(void) { begin(OP_SCAN); }
int  net_port_scan_state(void) { return s_op == OP_SCAN ? s_state : NET_IDLE; }
int  net_port_scan_results(net_ap_t *out, int max) { int n = s_n_aps < max ? s_n_aps : max; memcpy(out, s_aps, n * sizeof *out); return n; }
void net_port_connect_start(const char *ssid, const char *pass) {
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid); snprintf(s_pass, sizeof s_pass, "%s", pass ? pass : "");
    if (s_connected) { s_state = NET_DONE; s_op = OP_CONNECT; return; }   /* the retry after a failed check: still on */
    begin(OP_CONNECT);
}
int  net_port_connect_state(void) { return s_op == OP_CONNECT ? s_state : NET_IDLE; }
void net_port_check_start(void) { begin(OP_CHECK); }
int  net_port_check_state(const update_manifest_t **m) { if (s_op != OP_CHECK) return NET_IDLE; if (s_state == NET_DONE) *m = &s_m; return s_state; }
void net_port_install_start(void) { s_percent = 0; begin(OP_INSTALL); }
int  net_port_install_state(int *percent) { if (s_op != OP_INSTALL) return NET_IDLE; *percent = s_percent; return s_state; }
void net_port_abort(void) { s_abort = true; if (s_op == OP_CONNECT) esp_wifi_disconnect(); }
int  net_port_fail_reason(void) { return s_err; }
void net_port_off(void) {
    s_abort = true;
    if (s_task) {                                            /* let a download notice the abort before the driver goes */
        for (int i = 0; i < 100 && s_state == NET_BUSY; i++) vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(s_task); s_task = NULL;
    }
    if (s_up) {
        esp_wifi_disconnect(); esp_wifi_stop(); esp_wifi_deinit();
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi);
        if (s_netif) { esp_netif_destroy_default_wifi(s_netif); s_netif = NULL; }
        esp_event_loop_delete_default();
        s_up = false; s_connected = false;
        ESP_LOGI(TAG, "radio off: internal heap %u KB free", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    }
    s_op = OP_NONE; s_state = NET_IDLE;
}
