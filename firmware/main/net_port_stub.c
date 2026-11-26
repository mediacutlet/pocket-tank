/* net_port_stub.c - the network port of an image with NO RADIO
 * (CONFIG_POCKET_TANK_NO_RADIO, 2026-10-12): the P4 home tank until the
 * esp-hosted spike says how its C6 is driven (docs/board-p4-10.1.md). Every
 * network ask fails at once with NET_ERR_RADIO - the update page says so, the
 * clockless boot gets no time, provisioning stores nothing - and the board
 * marker every image must carry (net_port_esp.c) is here too, so the release
 * tools still read the board off the .bin. */
#include "update.h"
#include "version.h"
#include "net_time.h"
#include "wifi_nets.h"
#include "esp_log.h"
#include <string.h>

typedef struct { char magic[8]; char board[24]; } pt_board_marker_t;
const __attribute__((section(".rodata_custom_desc"), used)) pt_board_marker_t pt_board_marker = { PT_BOARD_MAGIC, PT_BOARD };

static const char *TAG = "net";
static int s_state = NET_IDLE;
static void refuse(const char *what) { ESP_LOGW(TAG, "%s: this image has no radio", what); s_state = NET_FAILED; }

const wifi_nets_t *net_port_nets(void) { static const wifi_nets_t none; return &none; }   /* (no saved network: wifi_nets.c reads this) */
const char *net_time_why(void) { return "no radio"; }
bool net_port_creds_set(const char *ssid, const char *pass) { (void)ssid; (void)pass; return false; }
bool net_port_creds_forget(void) { return false; }
bool net_port_creds_forget_one(const char *ssid) { (void)ssid; return false; }
int  net_port_scan_saved(const wifi_names_t *tried, char *ssid, char *pass) { (void)tried; if (ssid) ssid[0] = 0; if (pass) pass[0] = 0; return 0; }
bool net_port_board_code(char out[NET_CODE_LEN + 1]) { out[0] = 0; return false; }
int  net_port_crew(void) { return 0; }
int  net_time_missed(void) { return 0; }
void net_port_scan_start(void) { refuse("scan"); }
int  net_port_scan_state(void) { return s_state; }
int  net_port_scan_results(net_ap_t *out, int max) { (void)out; (void)max; return 0; }
void net_port_connect_start(const char *ssid, const char *pass) { (void)ssid; (void)pass; refuse("connect"); }
int  net_port_connect_state(void) { return s_state; }
void net_port_check_start(void) { refuse("update check"); }
int  net_port_check_state(const update_manifest_t **m) { if (m) *m = NULL; return s_state; }
void net_port_install_start(void) { refuse("install"); }
int  net_port_install_state(int *percent) { if (percent) *percent = 0; return s_state; }
void net_port_abort(void) { }
void net_port_off(void) { s_state = NET_IDLE; }
int  net_port_fail_reason(void) { return NET_ERR_RADIO; }
bool net_time_sync(int64_t *unix_out, int64_t *at_us, bool quick) { (void)unix_out; (void)at_us; (void)quick; return false; }
/* the director's bench knobs (director.c declares them itself) */
const char *net_port_manifest_url(void) { return "(no radio)"; }
bool net_port_set_manifest_url(const char *url) { (void)url; return false; }
void net_port_esp_connect_budget(int attempts, int wait_ms) { (void)attempts; (void)wait_ms; }
