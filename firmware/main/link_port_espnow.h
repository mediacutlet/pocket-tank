/* link_port_espnow.h — the tank link's radio (docs/tank-link-0.4.md): Wi-Fi in
 * station mode with NO network and the LEAN buffers phase 0 measured (16 KB of
 * internal RAM while up, nothing while down), ESP-NOW on a fixed channel. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_now.h"
#include "link.h"

/* the bare radio (the spike uses it with its own callbacks) */
bool lp_radio_up(esp_now_recv_cb_t on_recv, esp_now_send_cb_t on_sent);
void lp_radio_down(void);
bool lp_radio_is_up(void);
int  lp_radio_channel(void);            /* the ESP-NOW channel, 1..13 (default 1) */
bool lp_radio_set_channel(int ch);      /* `link chan <n>`: every tank in a row must share it */

/* the link's port: frames from the air wait in a PSRAM ring for the tank task;
 * one older than LP_STALE_MS when drained is dropped (the leases allow ~1.5 s) */
#define LP_STALE_MS 1000
bool link_port_start(void);
void link_port_stop(void);
void link_port_send(void *ctx, uint32_t to, const uint8_t *buf, int len);   /* link_host_t.send */
void link_port_drain(link_t *L, uint32_t now_ms);
void link_port_stats(uint32_t *tx, uint32_t *tx_fail, uint32_t *rx, uint32_t *stale, uint32_t *full);
/* the big screens (11-24 audit, finding 2): their radio is a chip next door that can take seconds to answer, so the
 * start runs on a radio task of the port's and LINK waits on a card meanwhile. begin() hands the start over (UP at once
 * if the radio is up already); begin_poll() says how it went, from the tank task. alive(): up, and its bus has not
 * failed since. The pocket tanks' own radio starts at once: link_port_start(). (link_port_hosted.c, link_port_stub.c) */
enum { LP_BEGIN_IDLE, LP_BEGIN_WAIT, LP_BEGIN_UP, LP_BEGIN_FAIL };
int  link_port_begin(void);
int  link_port_begin_poll(void);
bool link_port_alive(void);
void link_port_settle(int ms);              /* wait (at most ms) for the radio task's handed-over jobs: the sleep's goodbye */
