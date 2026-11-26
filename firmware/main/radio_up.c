/* radio_up.c - the hosted stack's bring-up and its health (radio_hosted.h), apart from radio_hosted.c so the rules can
 * run on the Mac (firmware/host_test/radio_up_host.c, tools/test_radio_ram.py; the 11-24 audit's P5-P7 stayed green
 * with the bug planted while only the source's words were checked).
 *  - the room check comes first: hosted asserts - a restart - when an allocation it needs fails (radio_mem.h);
 *  - a bus failure (hosted's TRANSPORT_FAILURE event, or the link's own count of requests in a row the C6 never
 *    answered: hosted 3.1.0 can stop receiving without posting the event, esp-hosted-mcu 200b5f5) marks the radio
 *    dead for the boot: every later ask says no at once instead of waiting out hosted's 5 s each;
 *  - one lock for the whole radio: the link's radio task, the C6 greeting after an update and the director's
 *    `radio` commands each bring the stack up or ask the relay, and two at once took each other's answer. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "eh_host_core.h"
#include "eh_host_sys.h"
#include "eh_host_api_types.h"
#include "eh_host_event.h"
#include "radio_hosted.h"
#include "radio_mem.h"

static const char *TAG = "radio";
static bool s_host, s_init;                   /* s_init: eh_host_init ran - the room check is for the first start only. (hosted remembers a
                                                failed bring-up: after NO_ANSWER every later call says no at once until the tank restarts) */
static radio_why_t s_why = RADIO_OK;
static eh_host_coprocessor_fwver_t s_ver;
static volatile bool s_dead;                  /* (audit 11-23) the bus to the C6 failed: the radio is down for this boot */

/* the lock: recursive (an ask brings the stack up under it), made once at boot (radio_hosted_boot, before any task can
   use the radio; static, so it cannot fail) */
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;
void radio_lock_init(void) { if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_buf); }
void radio_lock(void) {
    if (!s_lock) radio_lock_init();           /* (a caller before the boot's: only the tests) */
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}
void radio_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

radio_why_t radio_hosted_why(void) { return s_why; }
radio_fwver_t radio_hosted_ver(void) { radio_fwver_t v = { s_ver.major1, s_ver.minor1, s_ver.patch1 }; return v; }
static bool s_ver_said;                       /* the C6 answered hosted's version request (a factory 10.1 C6 did not, 10-20) */
bool radio_hosted_ver_said(void) { return s_ver_said; }
/* the radio is down for this boot: the link's sends fail and are counted, its doors lapse, LINK says it did not start */
void radio_hosted_mark_dead(const char *why) {
    if (!s_dead) ESP_LOGE(TAG, "the radio is down until the tank restarts (%s) - the tank goes on", why);
    s_dead = true;
}
/* hosted's transport-failure event (CONFIG_ESP_HOSTED_HOST_TRANSPORT_RESTART_ON_FAILURE is off: its default restarts the tank) */
static void on_transport_failure(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data;
    radio_hosted_mark_dead("the bus to the C6 failed");
}
bool radio_hosted_alive(void) { return s_host && !s_dead; }

bool radio_hosted_up(void) {
    if (s_dead) { s_why = RADIO_NO_ANSWER; return false; }
    if (s_host) return true;
    radio_lock();
    bool ok = false;
    do {
        if (s_dead) { s_why = RADIO_NO_ANSWER; break; }
        if (s_host) { ok = true; break; }   /* (another task brought it up while this one waited) */
        /* THE ROOM FIRST (11-23): hosted asserts - a restart - when an allocation it needs fails, so it is not started
         * at all unless internal RAM has room (radio_mem.h). The big buffers go to PSRAM (radio_mem.c). */
        const size_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), lg = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        radio_mem_log("before the stack");
        if (!s_init && !radio_mem_room(f, lg)) {
            s_why = RADIO_NO_ROOM;
            ESP_LOGE(TAG, "not started: needs %u KB internal (pieces %u KB), has %u KB (largest %u KB)",
                     (unsigned)(RADIO_MEM_INT_NEED / 1024), (unsigned)(RADIO_MEM_INT_BLOCK / 1024), (unsigned)(f / 1024), (unsigned)(lg / 1024));
            break;
        }
        unsigned h0 = (unsigned)(f / 1024);
        int e;
        if (!s_init) {
            esp_err_t ev = esp_event_loop_create_default();   /* (the failure event's loop: before the stack, which posts to it) */
            if ((ev == ESP_OK || ev == ESP_ERR_INVALID_STATE) && esp_event_handler_register(EH_HOST_EVENT, EH_HOST_EVENT_TRANSPORT_FAILURE, on_transport_failure, NULL) != ESP_OK)
                ESP_LOGW(TAG, "the transport-failure handler would not register");
            if ((e = eh_host_init(NULL)) != 0) { s_why = RADIO_NO_START; ESP_LOGE(TAG, "the hosted stack would not start (%d)", e); break; }
            s_init = true;
            radio_mem_log("stack started");
        }
        if ((e = eh_host_connect_to_slave()) != 0) { s_why = RADIO_NO_ANSWER; ESP_LOGE(TAG, "no answer from the C6 over SDIO (%d)", e); break; }
        memset(&s_ver, 0, sizeof s_ver);
        s_ver_said = eh_host_sys_get_cp_fw_version(&s_ver) == ESP_OK;
        if (!s_ver_said) { memset(&s_ver, 0, sizeof s_ver); ESP_LOGW(TAG, "the C6 would not say its firmware version"); }
        if (s_dead) { s_why = RADIO_NO_ANSWER; break; }   /* (the bus failed while it came up) */
        s_host = true; s_why = RADIO_OK; ok = true;
        ESP_LOGI(TAG, "the C6 is up: hosted firmware %lu.%lu.%lu | heap int %u -> %u KB", (unsigned long)s_ver.major1, (unsigned long)s_ver.minor1,
                 (unsigned long)s_ver.patch1, h0, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
        radio_mem_log("the C6 answered");
    } while (0);
    radio_unlock();
    return ok;
}
