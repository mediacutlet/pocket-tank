/* link_port_espnow.c — the tank link's radio (link_port_espnow.h). */
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "link_port_espnow.h"

static const char *TAG = "link";
#define LP_CHANNEL 1                 /* the default; `link chan <n>` moves it (2026-10-12: a radio parked on a Wi-Fi router's channel needs the pocket tanks to be able to follow it) */
static uint8_t s_chan = LP_CHANNEL;
#define LP_RING    24
#define LP_MACS    8

static bool s_up;
static const uint8_t BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* phase 0's LEAN config (docs/tank-link-0.4.md, "Phase 0 results"): TX
 * buffers dynamic (from PSRAM: SPIRAM_TRY_ALLOCATE_WIFI_LWIP), 2 static RX,
 * no AMPDU / AMSDU, the fewest management buffers, no event loop (ESP-NOW
 * needs no events; the driver logs two harmless "failed to post" lines). The
 * build's own config (16 STATIC TX buffers) does not fit beside the tank. */
bool lp_radio_up(esp_now_recv_cb_t on_recv, esp_now_send_cb_t on_sent) {
    if (s_up) return true;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;
    cfg.tx_buf_type = 1; cfg.static_tx_buf_num = 0; cfg.dynamic_tx_buf_num = 16;
    cfg.static_rx_buf_num = 2; cfg.dynamic_rx_buf_num = 8;
    cfg.ampdu_rx_enable = 0; cfg.ampdu_tx_enable = 0; cfg.amsdu_tx_enable = 0;
    cfg.rx_mgmt_buf_num = 2; cfg.mgmt_sbuf_num = 6;
    unsigned h0 = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "radio: wifi init %s (heap int %u KB)", esp_err_to_name(e), h0); return false; }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    if ((e = esp_wifi_start()) != ESP_OK) { ESP_LOGE(TAG, "radio: wifi start %s", esp_err_to_name(e)); esp_wifi_deinit(); return false; }
    esp_wifi_set_channel(s_chan, WIFI_SECOND_CHAN_NONE);
    if ((e = esp_now_init()) != ESP_OK) { ESP_LOGE(TAG, "radio: esp-now init %s", esp_err_to_name(e)); esp_wifi_stop(); esp_wifi_deinit(); return false; }
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
    esp_now_peer_info_t p = { 0 };
    memcpy(p.peer_addr, BCAST, 6); p.channel = 0; p.ifidx = WIFI_IF_STA;   /* 0 = whatever channel the radio is on */
    esp_now_add_peer(&p);
    s_up = true;
    ESP_LOGI(TAG, "radio up: channel %d | heap int %u -> %u KB", s_chan, h0, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    return true;
}
void lp_radio_down(void) {
    if (!s_up) return;
    esp_now_deinit(); esp_wifi_stop(); esp_wifi_deinit();
    s_up = false;
    ESP_LOGI(TAG, "radio down | heap int %u KB", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
}
bool lp_radio_is_up(void) { return s_up; }
int  lp_radio_channel(void) { return s_chan; }
bool lp_radio_set_channel(int ch) {          /* 1..13; applied now if the radio is up, else at the next radio up */
    if (ch < 1 || ch > 13) return false;
    s_chan = (uint8_t)ch;
    if (s_up) esp_wifi_set_channel(s_chan, WIFI_SECOND_CHAN_NONE);
    ESP_LOGI(TAG, "channel %d%s", ch, s_up ? " (now)" : " (at the next radio up)");
    return true;
}

/* ---- the link's port ---- */
typedef struct { uint32_t t_ms; int16_t len; int8_t rssi; uint8_t mac[6]; uint8_t b[LINK_FRAME_MAX]; } item_t;
static item_t *s_ring;                       /* PSRAM */
static volatile int s_head, s_n;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_port;
static struct { uint32_t uid; uint8_t mac[6]; uint32_t seen; } s_mac[LP_MACS];
static uint32_t s_tx, s_tx_fail, s_rx, s_stale, s_full;

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (!s_ring || len <= 0 || len > LINK_FRAME_MAX) return;
    portENTER_CRITICAL(&s_mux);
    if (s_n >= LP_RING) { s_full++; portEXIT_CRITICAL(&s_mux); return; }
    item_t *it = &s_ring[(s_head + s_n) % LP_RING];
    it->t_ms = (uint32_t)(esp_timer_get_time() / 1000); it->len = (int16_t)len;
    it->rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
    memcpy(it->mac, info->src_addr, 6); memcpy(it->b, data, (size_t)len);
    s_n++;
    portEXIT_CRITICAL(&s_mux);
}
static void on_sent(const uint8_t *mac, esp_now_send_status_t st) { (void)mac; if (st != ESP_NOW_SEND_SUCCESS) s_tx_fail++; }

/* learn which radio address speaks for which tank (a frame's sender ID is at
 * bytes 4..7 of its header); unicast needs the address as an ESP-NOW peer */
static void learn(uint32_t uid, const uint8_t *mac, uint32_t now) {
    if (!uid) return;
    int slot = -1, oldest = 0;
    for (int i = 0; i < LP_MACS; i++) {
        if (s_mac[i].uid == uid) { if (memcmp(s_mac[i].mac, mac, 6)) { esp_now_del_peer(s_mac[i].mac); slot = i; break; } s_mac[i].seen = now; return; }
        if (!s_mac[i].uid) { if (slot < 0) slot = i; }
        else if ((int32_t)(s_mac[i].seen - s_mac[oldest].seen) < 0) oldest = i;
    }
    if (slot < 0) { slot = oldest; esp_now_del_peer(s_mac[slot].mac); }
    s_mac[slot].uid = uid; memcpy(s_mac[slot].mac, mac, 6); s_mac[slot].seen = now;
    esp_now_peer_info_t p = { 0 };
    memcpy(p.peer_addr, mac, 6); p.channel = 0; p.ifidx = WIFI_IF_STA;
    if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&p);
}

bool link_port_start(void) {
    if (!s_ring) s_ring = heap_caps_calloc(LP_RING, sizeof(item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) { ESP_LOGE(TAG, "port: no PSRAM for the ring"); return false; }
    s_head = s_n = 0; memset(s_mac, 0, sizeof s_mac);
    s_tx = s_tx_fail = s_rx = s_stale = s_full = 0;
    if (!lp_radio_up(on_recv, on_sent)) return false;
    s_port = true;
    return true;
}
void link_port_stop(void) { if (s_port) { lp_radio_down(); s_port = false; } }

void link_port_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    (void)ctx;
    if (!s_port || len <= 0 || len > LINK_FRAME_MAX) return;
    const uint8_t *mac = BCAST;
    if (to) for (int i = 0; i < LP_MACS; i++) if (s_mac[i].uid == to) { mac = s_mac[i].mac; break; }   /* not learned yet: everyone (the rest ignore it) */
    s_tx++;
    if (esp_now_send(mac, buf, (size_t)len) != ESP_OK) s_tx_fail++;
}

void link_port_drain(link_t *L, uint32_t now_ms) {
    if (!s_port) return;
    for (;;) {
        item_t it;
        portENTER_CRITICAL(&s_mux);
        if (!s_n) { portEXIT_CRITICAL(&s_mux); break; }
        it = s_ring[s_head]; s_head = (s_head + 1) % LP_RING; s_n--;
        portEXIT_CRITICAL(&s_mux);
        if ((int32_t)(now_ms - it.t_ms) > LP_STALE_MS) { s_stale++; continue; }
        if (it.len >= 12) learn((uint32_t)it.b[4] | (uint32_t)it.b[5] << 8 | (uint32_t)it.b[6] << 16 | (uint32_t)it.b[7] << 24, it.mac, now_ms);
        s_rx++;
        link_rx(L, it.b, it.len, it.rssi, now_ms);
    }
}
void link_port_stats(uint32_t *tx, uint32_t *tx_fail, uint32_t *rx, uint32_t *stale, uint32_t *full) {
    *tx = s_tx; *tx_fail = s_tx_fail; *rx = s_rx; *stale = s_stale; *full = s_full;
}
