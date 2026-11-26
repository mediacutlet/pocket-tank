/* link_port_hosted.c — the tank link's radio port on the HOME TANK (the P4,
 * CONFIG_POCKET_TANK_RADIO_HOSTED, 2026-10-13).
 *
 * The P4 has no radio; an ESP32-C6 beside it has, reached over SDIO through
 * Espressif's hosted stack. Hosted gives the P4 Wi-Fi but not ESP-NOW, the
 * link the pocket tanks speak - so the C6 runs our firmware (firmware-c6/)
 * and relays ESP-NOW frames as hosted "peer data" messages
 * (common/now_relay.h). This file is the P4's end of that: the same port the
 * S3 tanks have in link_port_espnow.c, so the link engine above it
 * (common/link.c, link_dev.c) does not know the radio is next door.
 *
 * Nothing here runs until the link is asked for: the hosted stack comes up
 * on the first link_port_start (radio_hosted_up), Wi-Fi is started through
 * it as on any tank (station, no network), and NR_MSG_UP starts ESP-NOW on
 * the C6. A frame to send goes into a small queue and a task of ours hands
 * it to the bus: the tank's frame loop never waits on the C6.
 *
 * 11-24 (the radio audit): starting and stopping the radio waited on the C6 ON THE TANK TASK - every hosted request
 * waits up to 5 s for its answer, so a C6 that did not answer stopped the fish for 5-15 s on a LINK tap (the 20 s
 * watchdog restarts the tank). Now link_port_begin() hands the start to a radio task of ours and LINK shows a
 * waiting card until link_port_begin_poll() has the answer; link_port_stop() hands the goodbye over the same way.
 * A frame to send is stamped when queued and dropped if it waited too long (a visit's lease leaves 2 s: a frame
 * seconds late showed a visiting fish in two tanks at once); requests the C6 never answered, LP_DEAD_ROW in a row,
 * mark the radio down for the boot (hosted 3.1.0 can stop receiving without its failure event). */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "eh_host_feat_peer_data.h"
#include "link_port_espnow.h"
#include "link_spike.h"
#include "radio_hosted.h"
#include "now_relay.h"

static const char *TAG = "link";
#define LP_CHANNEL 1                        /* the default; `link chan <n>` moves it */
#define LP_RING    24
#define LP_MACS    8
#define LP_TXQ     16
#define LP_TX_STALE_MS 500                  /* a frame that waited longer for the bus is dropped, not sent late (the pocket port drops
                                               received ones past LP_STALE_MS; nothing on the S3 holds a frame at all) */
#define LP_DEAD_ROW 3                       /* sends in a row the C6 never answered: the radio is down */
#define LP_NOANSWER_MS 2000                 /* ... "never answered" = the send failed after waiting this long (hosted's request waits
                                               5 s, and fails at once on its own out-of-memory: that is not the C6's silence) */

static uint8_t s_chan = LP_CHANNEL;
static bool s_cb, s_up, s_port;
static const uint8_t BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* frames from the air, waiting for the tank task (as on the S3 port) */
typedef struct { uint32_t t_ms; int16_t len; int8_t rssi; uint8_t mac[6]; uint8_t b[LINK_FRAME_MAX]; } item_t;
static item_t *s_ring;                      /* PSRAM */
static volatile int s_head, s_n;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static struct { uint32_t uid; uint8_t mac[6]; uint32_t seen; } s_mac[LP_MACS];
static uint32_t s_tx, s_tx_fail, s_rx, s_stale, s_full;

/* frames to send, waiting for the bus */
typedef struct { uint32_t t_ms; uint16_t len; uint8_t b[sizeof(nr_frame_t) + LINK_FRAME_MAX]; } tx_t;
static QueueHandle_t s_txq;

/* the C6's last word (NR_MSG_STATE) */
static nr_state_t s_state;
static volatile unsigned s_state_len;       /* how much of it the C6 sent (a v1 relay's answer is shorter) */
static SemaphoreHandle_t s_state_sem;
static uint16_t s_req;                      /* (v2) the last request's number */
static uint8_t s_relay_ver;                 /* the relay's version, from its last answer (0: not heard yet) */

/* (11-24 audit) hosted 3.1.0 calls these with len > 0 and data NULL when its own copy failed: checked */
static void on_state(uint32_t id, const uint8_t *data, size_t len, void *ctx) {
    (void)id; (void)ctx;
    if (!data || len < NR_STATE_V1_LEN) return;
    memset(&s_state, 0, sizeof s_state);
    memcpy(&s_state, data, len < sizeof s_state ? len : sizeof s_state);   /* (a v1 relay's answer: the v2 tail stays 0) */
    s_state_len = (unsigned)len;
    if (s_state_sem) xSemaphoreGive(s_state_sem);
}
static void on_frame(uint32_t id, const uint8_t *data, size_t len, void *ctx) {
    (void)id; (void)ctx;
    if (!data || !s_ring || len <= sizeof(nr_frame_t) || len > sizeof(nr_frame_t) + LINK_FRAME_MAX) return;
    const nr_frame_t *h = (const nr_frame_t *)data;
    portENTER_CRITICAL(&s_mux);
    if (s_n >= LP_RING) { s_full++; portEXIT_CRITICAL(&s_mux); return; }
    item_t *it = &s_ring[(s_head + s_n) % LP_RING];
    it->t_ms = (uint32_t)(esp_timer_get_time() / 1000); it->len = (int16_t)(len - sizeof *h);
    it->rssi = h->rssi; memcpy(it->mac, h->mac, 6); memcpy(it->b, data + sizeof *h, len - sizeof *h);
    s_n++;
    portEXIT_CRITICAL(&s_mux);
}
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static volatile int s_row;                  /* sends in a row the C6 did not answer (each start begins again at 0) */
static void tx_task(void *arg) {
    static tx_t t;                          /* one at a time: off the stack */
    (void)arg;
    for (;;) {
        if (xQueueReceive(s_txq, &t, portMAX_DELAY) != pdTRUE) continue;
        if ((int32_t)(now_ms() - t.t_ms) > LP_TX_STALE_MS) { s_tx_fail++; continue; }   /* too late to mean what it said */
        if (!radio_hosted_alive()) { s_tx_fail++; continue; }                          /* (no 5 s wait on a dead bus) */
        const uint32_t t0 = now_ms();
        if (eh_host_peer_data_send(NR_MSG_SEND, t.b, t.len) != ESP_OK) {
            s_tx_fail++;
            if ((int32_t)(now_ms() - t0) >= LP_NOANSWER_MS && ++s_row >= LP_DEAD_ROW) radio_hosted_mark_dead("the C6 answered none of the link's last sends");
        } else s_row = 0;
    }
}
/* ask the C6 something and wait for its NR_MSG_STATE - the whole ask under the radio's lock (11-24 audit, finding 8:
   the greeting after a C6 update and LINK asked at once and took each other's answer) */
static bool ask(uint32_t msg, const void *data, size_t len, nr_state_t *out, int wait_ms);
bool radio_hosted_ask(uint32_t msg, const void *data, size_t len, nr_state_t *out, int wait_ms) {
    radio_lock(); bool ok = ask(msg, data, len, out, wait_ms); radio_unlock(); return ok;
}
static bool ask(uint32_t msg, const void *data, size_t len, nr_state_t *out, int wait_ms) {
    if (!radio_hosted_up()) return false;
    if (!s_state_sem) s_state_sem = xSemaphoreCreateBinary();
    if (!s_state_sem) return false;
    if (!s_cb) {
        if (eh_host_peer_data_register(NR_MSG_STATE, on_state, NULL) != ESP_OK ||
            eh_host_peer_data_register(NR_MSG_RECV, on_frame, NULL) != ESP_OK) { ESP_LOGE(TAG, "radio: relay messages not registered"); return false; }
        s_cb = true;
    }
    xSemaphoreTake(s_state_sem, 0);         /* (an old answer does not count) */
    /* (v2) every request numbered, in the version the relay speaks: UP / CHAN carry it in nr_up_t, the rest in nr_ask_t
       (a v1 relay ignores that body) */
    const uint16_t req = ++s_req ? s_req : ++s_req;
    uint8_t body[sizeof(nr_up_t) > sizeof(nr_ask_t) ? sizeof(nr_up_t) : sizeof(nr_ask_t)];
    const void *b = data; size_t blen = len;
    if ((msg == NR_MSG_UP || msg == NR_MSG_CHAN) && data && len >= NR_UP_V1_LEN) {
        nr_up_t u = { 0 }; memcpy(&u, data, NR_UP_V1_LEN);
        u.version = nr_speak(s_relay_ver); u.req = req;
        memcpy(body, &u, sizeof u); b = body; blen = u.version >= 2 ? sizeof u : NR_UP_V1_LEN;
    } else if (!data) {
        nr_ask_t a = { NR_VERSION, 0, req };
        memcpy(body, &a, sizeof a); b = body; blen = sizeof a;
    }
    esp_err_t e = eh_host_peer_data_send(msg, b, blen);
    if (e != ESP_OK) { ESP_LOGW(TAG, "radio: refused (0x%x)", (unsigned)e); return false; }
    const int64_t end = esp_timer_get_time() + (int64_t)wait_ms * 1000;
    for (;;) {                              /* an answer to an EARLIER request (late, v2 says so) is not this one's: wait on */
        const int64_t left = end - esp_timer_get_time();
        if (left <= 0 || xSemaphoreTake(s_state_sem, pdMS_TO_TICKS((uint32_t)(left / 1000) + 1)) != pdTRUE) {
            ESP_LOGW(TAG, "radio: no answer in %d ms", wait_ms); return false; }
        if (nr_answer_is(&s_state, s_state_len, req)) break;
        ESP_LOGW(TAG, "radio: late %u, waiting %u", (unsigned)s_state.req, (unsigned)req);
    }
    s_relay_ver = s_state.version;
    if (out) *out = s_state;
    return true;
}


enum { JOB_START = 1, JOB_STOP = 2, JOB_CHAN = 3 };   /* the radio task's jobs (below) */
static bool post(uint8_t job);

/* the bare radio is the S3's (the phase 0 spike drove ESP-NOW itself): not here */
bool lp_radio_up(esp_now_recv_cb_t on_recv, esp_now_send_cb_t on_sent) { (void)on_recv; (void)on_sent; ESP_LOGW(TAG, "radio: S3 only"); return false; }
void lp_radio_down(void) { }
bool lp_radio_is_up(void) { return s_up; }
int  lp_radio_channel(void) { return s_up && s_state.channel ? s_state.channel : s_chan; }
bool lp_radio_set_channel(int ch) {
    if (ch < 1 || ch > 13) return false;
    s_chan = (uint8_t)ch;
    if (s_up && !post(JOB_CHAN)) ESP_LOGW(TAG, "channel %d: busy, next up", ch);   /* (the C6 is asked on the radio task: never the tank task's wait) */
    ESP_LOGI(TAG, "channel %d%s", ch, s_up ? " (asked now)" : " (at the next radio up)");
    return true;
}

static void learn(uint32_t uid, const uint8_t *mac, uint32_t now) {   /* which radio address speaks for which tank */
    if (!uid) return;
    int slot = -1, oldest = 0;
    for (int i = 0; i < LP_MACS; i++) {
        if (s_mac[i].uid == uid) { memcpy(s_mac[i].mac, mac, 6); s_mac[i].seen = now; return; }
        if (!s_mac[i].uid) { if (slot < 0) slot = i; }
        else if ((int32_t)(s_mac[i].seen - s_mac[oldest].seen) < 0) oldest = i;
    }
    if (slot < 0) slot = oldest;
    s_mac[slot].uid = uid; memcpy(s_mac[slot].mac, mac, 6); s_mac[slot].seen = now;
}

/* the queue and its task in PSRAM, made once (11-24 audit, finding 7: they took ~8 KB of internal RAM before the room
   check, kept even when the radio never started, and a task that failed to start left frames counted as sent) */
static bool port_tasks(void) {
    if (!s_ring) s_ring = heap_caps_calloc(LP_RING, sizeof(item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) { ESP_LOGE(TAG, "port: no PSRAM for the ring"); return false; }
    if (!s_txq) {
        QueueHandle_t q = xQueueCreateWithCaps(LP_TXQ, sizeof(tx_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!q) { ESP_LOGE(TAG, "port: no queue"); return false; }
        s_txq = q;                          /* BEFORE the task: it may run at once and read it (11-24, the 10.1: an assert in xQueueReceive) */
        if (xTaskCreateWithCaps(tx_task, "pt_link_tx", 4096, NULL, 5, NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {   /* (it never touches flash) */
            ESP_LOGE(TAG, "port: no send task"); s_txq = NULL; vQueueDeleteWithCaps(q); return false; }
    }
    return true;
}
/* bring the relay up: the stack, its Wi-Fi, ESP-NOW on the C6 - waits on the C6, so never on the tank task (the radio task) */
static bool start_relay(nr_state_t *st) {
    unsigned h0 = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    if (!radio_hosted_wifi()) return false;   /* (the hosted stack and its Wi-Fi: up once, kept for the boot) */
    if (!s_relay_ver && !radio_hosted_ask(NR_MSG_PING, NULL, 0, st, 1500)) return false;   /* (v2) which version the relay speaks */
    nr_up_t u = { NR_VERSION, s_chan, 0 };
    if (!radio_hosted_ask(NR_MSG_UP, &u, sizeof u, st, 3000)) return false;
    if (!st->up) { ESP_LOGE(TAG, "radio: no ESP-NOW on the C6 (%d)", st->err); return false; }
    /* (0.4.3, review 8's M2) up is not on the air: the relay sends a probe at UP (and again at each ask) - a radio that never
       sends is a start that failed, so LINK says so (GET HELP where nothing can help) instead of a link page that never links */
    for (int k = 0; k < 4 && st->version >= 2 && !st->radio_ok; k++) { vTaskDelay(pdMS_TO_TICKS(400)); radio_hosted_ask(NR_MSG_PING, NULL, 0, st, 1500); }
    if (st->version >= 2 && !st->radio_ok) { ESP_LOGE(TAG, "radio: the C6's radio sends nothing"); return false; }
    ESP_LOGI(TAG, "radio up: ch %d%s | C6 %02x:%02x:%02x:%02x:%02x:%02x (v%d) | heap %u -> %u KB", st->channel,
             st->err == NR_ERR_CHANNEL ? " (the network's: the asked one was refused)" : "",
             st->mac[0], st->mac[1], st->mac[2], st->mac[3], st->mac[4], st->mac[5], st->version, h0, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    return true;
}

/* ---- the radio task: the start and the goodbye, off the tank task (11-24 audit, finding 2) ---- */
static QueueHandle_t s_jobs;
static volatile int s_jobs_n;               /* jobs posted and not yet finished (link_port_settle) */
static volatile int s_begin = LP_BEGIN_IDLE;   /* the start's state: IDLE, WAIT (the radio task has it), then UP or FAIL */
static void radio_task(void *arg) {
    (void)arg;
    for (;;) {
        uint8_t job;
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job == JOB_START) { nr_state_t st; s_begin = start_relay(&st) ? LP_BEGIN_UP : LP_BEGIN_FAIL; }
        else if (job == JOB_STOP) {
            radio_hosted_ask(NR_MSG_DOWN, NULL, 0, NULL, 1500);
            ESP_LOGI(TAG, "radio down | heap int %u KB", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
        } else if (job == JOB_CHAN) {
            nr_up_t u = { NR_VERSION, s_chan, 0 }; nr_state_t st;
            if (radio_hosted_ask(NR_MSG_CHAN, &u, sizeof u, &st, 2000) && st.err) ESP_LOGW(TAG, "channel %d refused (%d): on %d", u.channel, st.err, st.channel);
        }
        s_jobs_n--;
    }
}
static bool post(uint8_t job) {
    if (!s_jobs) {
        QueueHandle_t q = xQueueCreateWithCaps(4, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!q) return false;
        s_jobs = q;                         /* BEFORE the task (as s_txq above) */
        if (xTaskCreateWithCaps(radio_task, "pt_radio", 6144, NULL, 4, NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {   /* (no flash: hosted's
                                                                                   own tasks run on PSRAM stacks too) */
            ESP_LOGE(TAG, "radio: no radio task"); s_jobs = NULL; vQueueDeleteWithCaps(q); return false; }
    }
    s_jobs_n++;
    if (xQueueSend(s_jobs, &job, 0) == pdTRUE) return true;
    s_jobs_n--; return false;
}
/* wait (at most ms) for the radio task's jobs to finish: the sleep's goodbye reaches the C6 before the tank sleeps or
   powers off (the 11-24 review: an asynchronous goodbye was lost on those paths). The radio task's own waits are
   hosted's, bounded; this bounds the tank task's. */
void link_port_settle(int ms) {
    for (int t = 0; s_jobs_n > 0 && t < ms; t += 10) vTaskDelay(pdMS_TO_TICKS(10));
}
int link_port_begin(void) {
    if (s_port) return LP_BEGIN_UP;
    if (s_begin != LP_BEGIN_IDLE) return LP_BEGIN_WAIT;   /* (under way, or answered and not yet polled: the poll takes the answer - the 11-24
                                                             review: a second start reset the first one's ring and lost its answer) */
    if (!port_tasks()) return LP_BEGIN_FAIL;
    s_head = s_n = 0; memset(s_mac, 0, sizeof s_mac);
    s_tx = s_tx_fail = s_rx = s_stale = s_full = 0; s_row = 0;
    s_begin = LP_BEGIN_WAIT;
    if (!post(JOB_START)) { s_begin = LP_BEGIN_IDLE; return LP_BEGIN_FAIL; }
    return LP_BEGIN_WAIT;
}
int link_port_begin_poll(void) {
    const int b = s_begin;
    if (b == LP_BEGIN_UP) { s_begin = LP_BEGIN_IDLE; s_up = true; s_port = true; }
    else if (b == LP_BEGIN_FAIL) s_begin = LP_BEGIN_IDLE;
    return b == LP_BEGIN_IDLE ? (s_port ? LP_BEGIN_UP : LP_BEGIN_FAIL) : b;
}
bool link_port_alive(void) { return s_port && radio_hosted_alive(); }
/* the synchronous start is kept for callers that may wait (none on the tank task) */
bool link_port_start(void) {
    if (s_port) return true;
    if (!port_tasks()) return false;
    nr_state_t st;
    if (!start_relay(&st)) return false;
    s_up = true; s_port = true;
    return true;
}
void link_port_stop(void) {
    if (!s_port) return;
    s_up = false; s_port = false;                 /* the link stops sending now; the C6 hears its goodbye from the radio task */
    if (!post(JOB_STOP)) ESP_LOGW(TAG, "radio: no goodbye");
}
void link_port_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    (void)ctx;
    if (!s_port || len <= 0 || len > LINK_FRAME_MAX) return;
    static tx_t t;                          /* (the tank task is the only sender) */
    nr_frame_t *h = (nr_frame_t *)t.b;
    memcpy(h->mac, BCAST, 6); h->rssi = 0; h->pad = 0;
    if (to) for (int i = 0; i < LP_MACS; i++) if (s_mac[i].uid == to) { memcpy(h->mac, s_mac[i].mac, 6); break; }   /* not learned yet: everyone (the rest ignore it) */
    memcpy(t.b + sizeof *h, buf, (size_t)len); t.len = (uint16_t)(sizeof *h + len);
    t.t_ms = now_ms();                      /* (stamped as queued: tx_task drops it if it waits past LP_TX_STALE_MS) */
    s_tx++;
    if (xQueueSend(s_txq, &t, 0) != pdTRUE) s_tx_fail++;
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
/* the phase 0 spike measured the S3's own radio: nothing to measure here */
bool link_spike_on(int hz, int len, int preset) { (void)hz; (void)len; (void)preset; return lp_radio_up(NULL, NULL); }
void link_spike_rate(int hz, int len) { (void)hz; (void)len; }
void link_spike_off(void) { }
bool link_spike_is_on(void) { return false; }
