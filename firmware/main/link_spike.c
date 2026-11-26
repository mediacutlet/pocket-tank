/* link_spike.c — tank link phase 0 (docs/tank-link-0.4.md): can the radio run
 * beside the live tank? The director's `link spike on` brings up the Wi-Fi
 * driver in station mode with NO network (no netif, no lwIP, no DHCP, no TLS)
 * and ESP-NOW on a fixed channel, then broadcasts a small beacon. Every step's
 * internal-heap cost goes to the log, and every 10 s a line with what was sent,
 * what was heard from each other tank (signal strength, frames lost) and the
 * internal heap now / its low-water mark / its largest block. The tank's own
 * 10 s line carries fps and decision time as ever. `link spike off` takes it
 * all down again and logs what came back (a leak shows here).
 *
 * A measuring tool, not the link: nothing here touches the tank. */
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "link_spike.h"
#include "link_port_espnow.h"
#include "link_dev.h"

static const char *TAG = "link";

#define SPIKE_MAGIC 0x4b4e4c50u          /* "PLNK" */
#define SPIKE_PEERS 4
#define SPIKE_LOG_US (10 * 1000 * 1000)

typedef struct { uint32_t magic, seq; uint16_t heap_kb, heap_min_kb; } beacon_t;
typedef struct {
    uint8_t mac[6]; bool used;
    uint32_t rx, lost, last_seq; int rssi_sum, rssi_min, rssi_max;
    uint16_t their_heap_kb, their_min_kb;
} peer_t;

static bool s_up;
static esp_timer_handle_t s_beacon_t, s_log_t;
static uint32_t s_seq, s_tx_ok, s_tx_fail, s_rx_bad;
static int s_len = sizeof(beacon_t);
static peer_t s_peer[SPIKE_PEERS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static const uint8_t BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static unsigned heap_kb(void) { return (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024; }
static unsigned heap_min_kb(void) { return (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024; }
static unsigned heap_big_kb(void) { return (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024; }

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    beacon_t b;
    if (len < (int)sizeof b) { s_rx_bad++; return; }
    memcpy(&b, data, sizeof b);
    if (b.magic != SPIKE_MAGIC) { s_rx_bad++; return; }
    int rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    portENTER_CRITICAL(&s_mux);
    peer_t *p = NULL;
    for (int i = 0; i < SPIKE_PEERS && !p; i++) if (s_peer[i].used && !memcmp(s_peer[i].mac, info->src_addr, 6)) p = &s_peer[i];
    for (int i = 0; i < SPIKE_PEERS && !p; i++) if (!s_peer[i].used) {
        p = &s_peer[i]; memset(p, 0, sizeof *p); p->used = true; memcpy(p->mac, info->src_addr, 6);
        p->last_seq = b.seq - 1; p->rssi_min = 0; p->rssi_max = -127;
    }
    if (p) {
        if (b.seq > p->last_seq + 1) p->lost += b.seq - p->last_seq - 1;
        p->last_seq = b.seq; p->rx++;
        p->rssi_sum += rssi;
        if (rssi < p->rssi_min) p->rssi_min = rssi;
        if (rssi > p->rssi_max) p->rssi_max = rssi;
        p->their_heap_kb = b.heap_kb; p->their_min_kb = b.heap_min_kb;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void on_sent(const uint8_t *mac, esp_now_send_status_t st) {
    (void)mac;
    if (st == ESP_NOW_SEND_SUCCESS) s_tx_ok++; else s_tx_fail++;
}

static void beacon(void *arg) {
    (void)arg;
    uint8_t buf[ESP_NOW_MAX_DATA_LEN];         /* (the esp_timer task's stack, not .bss) */
    beacon_t b = { SPIKE_MAGIC, ++s_seq, (uint16_t)heap_kb(), (uint16_t)heap_min_kb() };
    memcpy(buf, &b, sizeof b);
    if (esp_now_send(BCAST, buf, s_len) != ESP_OK) s_tx_fail++;
}

static void log_window(void *arg) {
    (void)arg;
    peer_t snap[SPIKE_PEERS];
    portENTER_CRITICAL(&s_mux);
    memcpy(snap, s_peer, sizeof snap);
    for (int i = 0; i < SPIKE_PEERS; i++) if (s_peer[i].used) {
        s_peer[i].rx = s_peer[i].lost = 0; s_peer[i].rssi_sum = 0; s_peer[i].rssi_min = 0; s_peer[i].rssi_max = -127;
    }
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "spike 10 s | tx ok %lu fail %lu (%d B) bad rx %lu | heap int %u KB, low-water %u KB, largest block %u KB",
             (unsigned long)s_tx_ok, (unsigned long)s_tx_fail, s_len, (unsigned long)s_rx_bad, heap_kb(), heap_min_kb(), heap_big_kb());
    s_tx_ok = s_tx_fail = 0;
    bool any = false;
    for (int i = 0; i < SPIKE_PEERS; i++) {
        peer_t *p = &snap[i];
        if (!p->used) continue;
        any = true;
        if (!p->rx) { ESP_LOGI(TAG, "  " MACSTR ": nothing heard this window", MAC2STR(p->mac)); continue; }
        ESP_LOGI(TAG, "  " MACSTR ": rx %lu lost %lu | rssi avg %d min %d max %d | its heap int %u KB, low-water %u KB",
                 MAC2STR(p->mac), (unsigned long)p->rx, (unsigned long)p->lost,
                 p->rssi_sum / (int)p->rx, p->rssi_min, p->rssi_max, p->their_heap_kb, p->their_min_kb);
    }
    if (!any) ESP_LOGI(TAG, "  no other tank heard yet");
}

/* (phase 0 compared the build's Wi-Fi config - it fails beside the tank - with
 * the LEAN one; only the lean one is left: lp_radio_up, link_port_espnow.c) */
bool link_spike_on(int hz, int len, int preset) {
    (void)preset;
    if (s_up) { ESP_LOGW(TAG, "spike already on"); return true; }
    if (link_dev_on()) { ESP_LOGW(TAG, "the link has the radio: link off first"); return false; }
    if (len < (int)sizeof(beacon_t)) len = sizeof(beacon_t);
    if (len > ESP_NOW_MAX_DATA_LEN) len = ESP_NOW_MAX_DATA_LEN;
    s_len = len;
    unsigned h0 = heap_kb();
    if (!lp_radio_up(on_recv, on_sent)) return false;
    memset(s_peer, 0, sizeof s_peer);
    s_seq = s_tx_ok = s_tx_fail = s_rx_bad = 0;
    const esp_timer_create_args_t bt = { .callback = beacon, .name = "link_beacon" };
    const esp_timer_create_args_t lt = { .callback = log_window, .name = "link_log" };
    esp_timer_create(&bt, &s_beacon_t);
    esp_timer_create(&lt, &s_log_t);
    esp_timer_start_periodic(s_beacon_t, 1000000 / (hz > 0 ? hz : 2));
    esp_timer_start_periodic(s_log_t, SPIKE_LOG_US);
    uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "spike up: " MACSTR ", %d beacons/s of %d B | radio cost %d KB, heap int %u KB, largest block %u KB",
             MAC2STR(mac), hz > 0 ? hz : 2, s_len, (int)h0 - (int)heap_kb(), heap_kb(), heap_big_kb());
    s_up = true;
    return true;
}

void link_spike_rate(int hz, int len) {
    if (!s_up) { ESP_LOGW(TAG, "spike is off"); return; }
    if (len >= (int)sizeof(beacon_t) && len <= ESP_NOW_MAX_DATA_LEN) s_len = len;
    esp_timer_stop(s_beacon_t);
    esp_timer_start_periodic(s_beacon_t, 1000000 / (hz > 0 ? hz : 2));
    ESP_LOGI(TAG, "spike: %d beacons/s of %d B", hz > 0 ? hz : 2, s_len);
}

void link_spike_off(void) {
    if (!s_up) { ESP_LOGW(TAG, "spike is off"); return; }
    unsigned h = heap_kb();
    esp_timer_stop(s_beacon_t); esp_timer_delete(s_beacon_t);
    esp_timer_stop(s_log_t); esp_timer_delete(s_log_t);
    lp_radio_down();
    s_up = false;
    ESP_LOGI(TAG, "spike off | heap int %u KB -> %u KB, low-water %u KB, largest block %u KB", h, heap_kb(), heap_min_kb(), heap_big_kb());
}

bool link_spike_is_on(void) { return s_up; }
