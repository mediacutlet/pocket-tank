/* link_port_stub.c - the tank link's radio port and the radio spike, in an
 * image with NO RADIO (CONFIG_POCKET_TANK_NO_RADIO, 2026-10-12): the P4 home
 * tank until the esp-hosted spike (docs/board-p4-10.1.md). `link on` finds no
 * radio and says so; nothing is sent, nothing arrives. */
#include "link_port_espnow.h"
#include "link_spike.h"
#include "esp_log.h"

static const char *TAG = "link";
bool lp_radio_up(esp_now_recv_cb_t on_recv, esp_now_send_cb_t on_sent) { (void)on_recv; (void)on_sent; ESP_LOGW(TAG, "radio: this image has no radio"); return false; }
void lp_radio_down(void) { }
bool lp_radio_is_up(void) { return false; }
int  lp_radio_channel(void) { return 0; }
bool lp_radio_set_channel(int ch) { (void)ch; return false; }
bool link_port_start(void) { return lp_radio_up(NULL, NULL); }
void link_port_stop(void) { }
int  link_port_begin(void) { return lp_radio_up(NULL, NULL) ? LP_BEGIN_UP : LP_BEGIN_FAIL; }   /* (no radio: refused at once) */
int  link_port_begin_poll(void) { return LP_BEGIN_FAIL; }
bool link_port_alive(void) { return false; }
void link_port_settle(int ms) { (void)ms; }
void link_port_send(void *ctx, uint32_t to, const uint8_t *buf, int len) { (void)ctx; (void)to; (void)buf; (void)len; }
void link_port_drain(link_t *L, uint32_t now_ms) { (void)L; (void)now_ms; }
void link_port_stats(uint32_t *tx, uint32_t *tx_fail, uint32_t *rx, uint32_t *stale, uint32_t *full) { *tx = *tx_fail = *rx = *stale = *full = 0; }
bool link_spike_on(int hz, int len, int preset) { (void)hz; (void)len; (void)preset; return lp_radio_up(NULL, NULL); }
void link_spike_rate(int hz, int len) { (void)hz; (void)len; }
void link_spike_off(void) { }
bool link_spike_is_on(void) { return false; }
