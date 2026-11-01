/* update_mode.c - see update_mode.h */
#include "update_mode.h"
#include <string.h>
#include "update.h"
#include "tank.h"
#include "display_port.h"
#include "touch_port.h"
#include "battery_port.h"
#include "brightness.h"
#include "battery.h"
#include "director.h"
#include "imu_port.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "update";
#define REQ_MAGIC 0x4f54417au              /* "OTAz" */
RTC_NOINIT_ATTR static uint32_t s_req, s_req_check;

void update_mode_request(void) {
    s_req = REQ_MAGIC; s_req_check = ~REQ_MAGIC;
    ESP_LOGI(TAG, "restarting into update mode");
    vTaskDelay(pdMS_TO_TICKS(150));        /* the log line out the door */
    esp_restart();
}
bool update_mode_pending(void) {
    bool yes = s_req == REQ_MAGIC && s_req_check == ~REQ_MAGIC;
    s_req = 0; s_req_check = 0;            /* one boot only, whatever happens next */
    return yes;
}

static int s_way = -1;                     /* director `ota flip`: held, -1 = the IMU's */
void update_mode_hold_way(int q) { s_way = q; }
void update_mode_run(uint16_t *fb, int way_up) {
    const esp_partition_t *run = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "update mode: running from %s | internal heap %u KB, psram %u KB free (the measurement docs/OTA.md waits for)",
             run ? run->label : "?", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    float frac = 0; bool charging = false;
    int pct = battery_port_read(&frac, &charging) ? (int)(frac * 100 + 0.5f) : -1;
    bool on_power = pct >= 0 && BAT_ON_POWER(battery_port_state());
    ESP_LOGI(TAG, "battery %d%%, %s", pct, on_power ? "on the cable" : "on battery");
    director_init();                       /* the console, for the hands-free test channel (main.c's later call is a no-op) */
    update_begin(pct, on_power);
    int64_t last = esp_timer_get_time(), t0 = last, last_log = last;
    int last_page = -1;
    while (!update_outcome()) {
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f; last = now; if (dt > 0.25f) dt = 0.25f;
        imu_port_poll(now);                /* the pages turn with the tank, as the tank does (unless it was held) */
        { int q = s_way >= 0 ? s_way : (way_up & UPD_WAY_FIXED) ? (way_up & 3) : imu_port_quarter();
          display_port_set_quarter(q); touch_port_set_quarter(q); }
        float x = 0, y = 0; bool down = touch_port_read_raw(&x, &y);
        { static bool was;
          if (down && !was) {
              int tx, ty, i = update_calib_target(&tx, &ty);
              if (i >= 0) ESP_LOGI(TAG, "calib %d: target %d,%d press at %.0f,%.0f", i, tx, ty, x, y);
              else ESP_LOGI(TAG, "press at %.0f,%.0f -> %s", x, y, update_hit_name(update_hit(x, y)));
          }
          was = down; }
        update_touch(x, y, down);
        director_poll(NULL);               /* wifi set / ota tap / ota page: the test channel's hands */
        update_tick(dt);
        if (update_page() != last_page) { last_page = update_page(); ESP_LOGI(TAG, "page %d", last_page); }
        if (fb) { render_update(fb, TANK_W, (now - t0) / 1e6f); display_port_flush(fb); }
        brightness_apply(false);
        int k = battery_port_key_poll();   /* the PWR key: a long press cuts the power, as in the tank */
        if (k == 2) { ESP_LOGW(TAG, "PWR held in update mode: power off"); net_port_abort(); net_port_off(); display_port_sleep(); battery_port_poweroff(); esp_restart(); }
        if (now - last_log > 10000000) { last_log = now; ESP_LOGI(TAG, "page %d | internal heap %u KB free", update_page(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024); }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    if (update_outcome() == UPD_RESTART) {
        const esp_partition_t *next = esp_ota_get_boot_partition();
        ESP_LOGI(TAG, "installed: restarting into %s", next ? next->label : "?");
        net_port_off();
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
    ESP_LOGI(TAG, "back to the tank");
    net_port_off();
}

/* ---- provisioning at install (update_mode.h) ---- */
#define PROVISION_WAIT_S  8       /* the page asks within a second or two of the restart; it gives up after 10 */
#define PROVISION_IDLE_S  25      /* its open Wi-Fi form rescans every 3 s: this long without a word = skipped or closed */
#define PROVISION_OPEN_S  60      /* before the form opens the page is silent ("Installation complete", Next): the keeper's time to click */
#define PROV_MAGIC 0x50524f56u             /* "PROV": the request rides in RTC memory, as update mode's */
RTC_NOINIT_ATTR static uint32_t s_prov, s_prov_check;
static bool s_prov_asked;                  /* this boot was asked for by the page's scan: answer it */
void provision_mode_request(void) {
    s_prov = PROV_MAGIC; s_prov_check = ~PROV_MAGIC;
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}
bool provision_mode_wanted(void) {
    s_prov_asked = s_prov == PROV_MAGIC && s_prov_check == ~PROV_MAGIC;
    s_prov = 0; s_prov_check = 0;          /* one boot only */
    if (s_prov_asked) return true;
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    return esp_reset_reason() == ESP_RST_USB && !net_port_creds_get(ssid, pass);
}
void provision_mode_run(uint16_t *fb) {
    if (s_prov_asked) ESP_LOGI(TAG, "provisioning: the installer page asked the tank for the networks - the glass stays dark, the list is on its way");
    else ESP_LOGI(TAG, "provisioning: a reset over USB and no network saved - the glass stays dark while the installer page may ask (%d s)", PROVISION_WAIT_S);
    if (fb) { memset(fb, 0, (size_t)TANK_W * TANK_H * sizeof *fb); display_port_flush(fb); }
    director_init();
    director_provision_begin();
    if (s_prov_asked) director_provision_scan();
    int64_t t0 = esp_timer_get_time(); bool radio = false; const char *why = "the page never asked";
    for (;;) {
        director_poll(NULL);
        director_provision_tick();
        int64_t now = esp_timer_get_time(), last = director_provision_last_us();
        if (director_provision_busy()) radio = true;
        if (director_provision_done()) { why = "a network is stored"; break; }
        if (!last && now - t0 > PROVISION_WAIT_S * 1000000LL) break;
        if (last && !director_provision_busy() &&
            now - last > (director_provision_form() ? PROVISION_IDLE_S : PROVISION_OPEN_S) * 1000000LL) {
            why = director_provision_form() ? "the page went quiet" : "the Wi-Fi form never opened"; break; }
        float x = 0, y = 0;
        if (touch_port_read_raw(&x, &y)) { why = "a touch on the glass"; break; }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    director_provision_end();
    ESP_LOGI(TAG, "provisioning over: %s", why);
    if (!radio) return;                     /* nothing was allocated: on with the boot */
    vTaskDelay(pdMS_TO_TICKS(400));         /* the page reads its answer */
    net_port_abort(); net_port_off();
    esp_restart();                          /* a software restart: the tank comes up on a clean heap, and this mode is not wanted again */
}
