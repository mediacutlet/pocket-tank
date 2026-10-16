/* net_port_stub.c — the net port for a board with NO RADIO (the M5Stack Tab5,
 * ESP32-P4, 2026-10-16: the P4 has no Wi-Fi or Bluetooth of its own; the
 * Tab5's Wi-Fi 6 comes from an ESP32-C6 on SDIO, which this port does not
 * bring up yet).
 *
 * The net port is an interface, not an assumption (common/update.h): this file
 * answers every call the way a board with no radio must, so the network
 * questions come back "no radio" instead of hanging. What still works: the
 * tank's own pages, the saves, the model, the glass. What does not: updates
 * over the air (the cable and the installer page still work), the first-run
 * Wi-Fi step, and the clock from the internet.
 *
 * It keeps the same NVS store as the S3 port (namespace "wifi": ssid / pass /
 * url), so a save carried over - and the day the C6 is brought up - finds the
 * keeper's network where it was left.
 *
 * It also owns the image's board marker (the S3 port keeps it in net_port_esp.c
 * because its OTA download checks it back): tools/pt_boards.py reads PT_BOARD
 * out of a .bin at PT_BOARD_MARKER_OFFSET. This file must keep the layout
 * (".rodata_custom_desc", a magic and the board id) and the -u pt_board_marker
 * in firmware/main/CMakeLists.txt. */
#include "update.h"
#include "version.h"
#include "net_time.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "net";
#define WIFI_NVS_NS "wifi"
#define OTA_MANIFEST_URL "https://github.com/mediacutlet/pocket-tank/releases/latest/download/latest-" PT_BOARD ".json"

typedef struct { char magic[8]; char board[24]; } pt_board_marker_t;
const __attribute__((section(".rodata_custom_desc"), used)) pt_board_marker_t pt_board_marker = { PT_BOARD_MAGIC, PT_BOARD };

/* ---- the store (identical to net_port_esp.c) ---- */
static bool nvs_get_str_ns(const char *key, char *out, size_t cap)
{
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = cap; esp_err_t e = nvs_get_str(h, key, out, &n); nvs_close(h);
    if (e != ESP_OK) { out[0] = 0; return false; }
    return true;
}
bool net_port_creds_get(char *ssid, char *pass)
{
    if (!nvs_get_str_ns("ssid", ssid, NET_SSID_MAX + 1) || !ssid[0]) return false;
    if (!nvs_get_str_ns("pass", pass, NET_PASS_MAX + 1)) pass[0] = 0;
    return true;
}
bool net_port_creds_set(const char *ssid, const char *pass)
{
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, "ssid", ssid) == ESP_OK && nvs_set_str(h, "pass", pass ? pass : "") == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    ESP_LOGW(TAG, "network saved (%s) - this chip has no radio to use it with yet", ssid);
    return ok;
}
void net_port_creds_forget(void)
{
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "ssid"); nvs_erase_key(h, "pass"); nvs_commit(h); nvs_close(h);
}
const char *net_port_manifest_url(void)
{
    static char url[NET_URL_MAX + 1];
    if (nvs_get_str_ns("url", url, sizeof url) && url[0]) return url;
    return OTA_MANIFEST_URL;
}
bool net_port_set_manifest_url(const char *url)
{
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = url && url[0] ? nvs_set_str(h, "url", url) == ESP_OK : (nvs_erase_key(h, "url"), true);
    ok = nvs_commit(h) == ESP_OK && ok; nvs_close(h);
    return ok;
}

/* ---- the radio: every operation answers NET_FAILED with NET_ERR_RADIO, at
 * once, so no page waits on it. NET_ERR_RADIO is the reason the flow shows. */
static int s_op, s_state = NET_IDLE, s_err = NET_ERR_NONE;

static void no_radio(int op)
{
    s_op = op; s_state = NET_FAILED; s_err = NET_ERR_RADIO;
    ESP_LOGW(TAG, "network op %d asked for: this board has no radio (net_port_stub.c)", op);
}
void net_port_scan_start(void) { no_radio(1); }
int  net_port_scan_state(void) { return s_state; }
int  net_port_scan_results(net_ap_t *out, int max) { (void)out; (void)max; return 0; }
void net_port_connect_start(const char *ssid, const char *pass) { (void)ssid; (void)pass; no_radio(2); }
int  net_port_connect_state(void) { return s_state; }
void net_port_check_start(void) { no_radio(3); }
int  net_port_check_state(const update_manifest_t **m) { if (m) *m = NULL; return s_state; }
void net_port_install_start(void) { no_radio(4); }
int  net_port_install_state(int *percent) { if (percent) *percent = 0; return s_state; }
void net_port_abort(void) {}
void net_port_off(void) { s_op = 0; s_state = NET_IDLE; }
int  net_port_fail_reason(void) { return s_err; }
void net_port_esp_connect_budget(int attempts, int wait_ms) { (void)attempts; (void)wait_ms; }
bool net_time_sync(int64_t *unix_out, int64_t *at_us) { (void)unix_out; (void)at_us; return false; }
