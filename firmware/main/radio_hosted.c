/* radio_hosted.c — the home tank's radio chip from the P4's side (radio_hosted.h). */
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_rom_crc.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "eh_host_sys.h"
#include "eh_host_api_types.h"
#include "eh_host_cp_ota.h"
#include "progression.h"
#include "radio_hosted.h"
#include "radio_mem.h"
#pragma GCC optimize ("Os")                    /* (size: the 10.1's radio image is a few KB under its slot; nothing here is on the frame's path) */

static const char *TAG = "radio";
/* the bring-up, its health and the radio's lock: radio_up.c */

static bool wifi_up(void) {
    static bool s_wifi;
    if (s_wifi) return true;
    if (!radio_hosted_up()) return false;
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { ESP_LOGE(TAG, "wifi: event loop 0x%x", (unsigned)e); return false; }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;
    if ((e = esp_wifi_init(&cfg)) != ESP_OK) { ESP_LOGE(TAG, "wifi: init 0x%x", (unsigned)e); return false; }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if ((e = esp_wifi_start()) != ESP_OK) { ESP_LOGE(TAG, "wifi: start 0x%x", (unsigned)e); return false; }
    esp_wifi_set_ps(WIFI_PS_NONE);
    s_wifi = true;
    radio_mem_log("wifi started");
    return true;
}
bool radio_hosted_wifi(void) { radio_lock(); bool ok = wifi_up(); radio_unlock(); return ok; }

/* THE HANDOVER's glue (its every decision: c6_ho.h's c6_ho_step). The state lives here; each event goes through
   ho_step, which saves the record BEFORE the caller acts on the answer. The tank task's ticks never wait on it. */
static c6_ho_state_t s_hs;
static SemaphoreHandle_t s_hl; static StaticSemaphore_t s_hlb;
static bool rec_save(const c6_rec_t *r);
static int ho_step(const c6_ho_ev_t *e, bool tick) {
    if (xSemaphoreTake(s_hl, tick ? 0 : portMAX_DELAY) != pdTRUE) return C6A_NONE;   /* (a tick skips a poll: the next one asks again) */
    int a = c6_ho_step(&s_hs, e);
    if ((a & C6A_SAVE) && !s_hs.nosave && !rec_save(&s_hs.r)) {   /* (the 11-24 review: a save that did not land is not acted on) */
        const c6_ho_ev_t f = { .type = C6E_SAVE_FAILED }; a = c6_ho_step(&s_hs, &f);
    }
    xSemaphoreGive(s_hl);
    return C6A_OF(a);
}
/* the console's `radio c6 update` (a bench path, not the handover): its own greet mark, read once at boot */
static void greet_mark(bool on) {
    nvs_handle_t h;
    if (nvs_open("radio", NVS_READWRITE, &h) != ESP_OK) return;
    if (on) nvs_set_u8(h, "c6chk", 1); else nvs_erase_key(h, "c6chk");
    nvs_commit(h); nvs_close(h);
}
static bool stage_read(c6_stage2_t *h, bool verify, bool relay_only);   /* below */
static void c6_hold_now(const char *when);
/* THE GUARD's other half (firmware-c6/main/guard.c): ESP-NOW up once, the relay's probe out, pings until its guard says
   confirmed, down again - the guard confirms only on a real send. st: the last answer (false: none) */
static bool probe(nr_state_t *st) {
    nr_up_t u = { NR_VERSION, 1, 0 }; bool got = false;
    if (radio_hosted_wifi() && (got = radio_hosted_ask(NR_MSG_UP, &u, sizeof u, st, 3000)))   /* (review 8's L4: st is the last ANSWER - an ask */
        for (int k = 0; k < 5 && st->guard_s != 0; k++) { vTaskDelay(pdMS_TO_TICKS(700)); radio_hosted_ask(NR_MSG_PING, NULL, 0, st, 2000); }   /* that fails
                                                     leaves it as it was: a lost last ping is no "no") */
    radio_hosted_ask(NR_MSG_DOWN, NULL, 0, NULL, 1500);
    return got;
}
/* after a write the C6 runs a new image on probation: the P4 greets it without being asked (the record's greet mark) */
static void greet_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));            /* the tank first */
    radio_lock();                               /* (the whole greeting under one hold: the 11-24 review) */
    nr_state_t st; bool heard = false;
    for (int k = 0; k < 3 && !heard; k++) if (!(heard = radio_hosted_up() && radio_hosted_ask(NR_MSG_PING, NULL, 0, &st, 2000))) vTaskDelay(pdMS_TO_TICKS(5000));
    c6_ho_ev_t e = { .type = C6E_GREET_HEARD, .up = radio_hosted_alive(), .silent = radio_hosted_why() == RADIO_NO_ANSWER, .st = heard ? &st : NULL };
    int a = ho_step(&e, false);
    if (a == C6A_PROBE) { const bool got = probe(&st); c6_ho_ev_t p = { .type = C6E_PROBED, .st = got ? &st : NULL }; a = ho_step(&p, false); }
    else if (a == C6A_HOLD) c6_hold_now("the greeting");
    ESP_LOGI(TAG, "c6: greeted: %s", heard ? (s_hs.r.ok ? "confirmed" : "on probation") : "no relay");
    radio_unlock(); vTaskDelete(NULL);
}
void radio_hosted_boot(void) {
    radio_lock_init();
    s_hl = xSemaphoreCreateMutexStatic(&s_hlb);
    c6_stage2_t h; nvs_handle_t n; uint8_t bench = 0, *raw = NULL; size_t sz = 0; int got = 0;
    const esp_reset_reason_t why = esp_reset_reason();
    c6_ho_ev_t e = { .type = C6E_BOOT, .reset = why == ESP_RST_POWERON ? C6R_POWERON : why == ESP_RST_SW ? C6R_SOFTWARE : C6R_CRASH };
    if (stage_read(&h, false, true)) { memcpy(e.sha, h.sha256, 8); e.stage_rv = (uint8_t)h.relay_version; e.size = h.size;
        if (esp_flash_read(NULL, e.elf8, C6_STAGE_OFFSET + sizeof h + 176, 8) != ESP_OK) e.size = 0; }   /* (its build: app_elf_sha256) */
    if (nvs_open("radio", NVS_READONLY, &n) == ESP_OK) {
        nvs_get_u8(n, "c6chk", &bench);
        if (nvs_get_blob(n, "c6rec", NULL, &sz) == ESP_OK && sz) got = (raw = malloc(sz)) != NULL && nvs_get_blob(n, "c6rec", raw, &sz) == ESP_OK ? 1 : -1;   /* (read by its length) */
        nvs_close(n);
    }
    if (bench) greet_mark(false);
    e.bench = bench; e.raw = raw; e.n = got > 0 ? (uint16_t)sz : 0; e.unreadable = got < 0;
    if (ho_step(&e, false) == C6A_GREET) { ESP_LOGI(TAG, "c6: greeting the C6"); xTaskCreate(greet_task, "pt_c6_greet", 6144, NULL, 3, NULL); }
    free(raw);
    radio_hosted_report();                      /* (the tank check's restart reads a startup: the radio's last state is in it) */
}

/* the staged image: its header (c6_stage.h), and with `verify` the image's own head and its CRC32 and SHA-256 */
#include "mbedtls/sha256.h"
static bool stage_read(c6_stage2_t *h, bool verify, bool relay_only) {
    const esp_partition_t *run = esp_ota_get_running_partition();   /* (the stage is ota_1: never the app that runs) */
    if (run && run->address < C6_STAGE_OFFSET + sizeof *h + C6_STAGE_MAX && C6_STAGE_OFFSET < run->address + run->size) {
        ESP_LOGE(TAG, "c6: stage 0x%x is the app's slot", (unsigned)C6_STAGE_OFFSET); return false; }
    uint8_t head[128];                        /* (on the caller's stack: the tank task reads a header while the handover verifies) */
    if (esp_flash_read(NULL, h, C6_STAGE_OFFSET, sizeof *h) != ESP_OK || esp_flash_read(NULL, head, C6_STAGE_OFFSET + sizeof *h, sizeof head) != ESP_OK) {
        ESP_LOGE(TAG, "c6: the P4's flash would not read at 0x%x", (unsigned)C6_STAGE_OFFSET); return false; }
    const int c = c6_stage_check(h, head, sizeof head, relay_only);
    if (c == C6S_NO_STAGE) { ESP_LOGI(TAG, "c6: no image staged"); return false; }
    if (c != C6S_OK) { ESP_LOGE(TAG, "c6: the staged image is refused (%d: %s)", c, c == C6S_NOT_C6 ? "not a C6 image" : c == C6S_NOT_RELAY ? "not our relay" : "a bad header or image"); return false; }
    if (!verify) return true;
    static uint8_t buf[2048];                 /* (verify runs under the radio lock only: the handover, the console's radio task) */
    uint32_t crc = 0; uint8_t sha[32];
    mbedtls_sha256_context x; mbedtls_sha256_init(&x); mbedtls_sha256_starts(&x, 0);
    bool ok = true;
    for (uint32_t off = 0; off < h->size && ok; off += sizeof buf) {
        uint32_t n = h->size - off < sizeof buf ? h->size - off : sizeof buf;
        if (esp_flash_read(NULL, buf, C6_STAGE_OFFSET + sizeof *h + off, n) != ESP_OK) { ESP_LOGE(TAG, "c6: read failed at %lu", (unsigned long)off); ok = false; break; }
        crc = esp_rom_crc32_le(crc, buf, n); mbedtls_sha256_update(&x, buf, n);
    }
    mbedtls_sha256_finish(&x, sha); mbedtls_sha256_free(&x);
    if (!ok) return false;
    if (crc != h->crc32) { ESP_LOGE(TAG, "c6: stage crc %08lx != %08lx", (unsigned long)crc, (unsigned long)h->crc32); return false; }
    if (memcmp(sha, h->sha256, 32)) { ESP_LOGE(TAG, "c6: stage damaged (sha)"); return false; }
    return true;
}
/* push a verified stage into the C6's spare slot: the chunks. The C6 switches only at the end (c6_finish): anything that
   stops it before leaves the C6 on its old firmware. C6X_OK: every chunk taken; else where it stopped (off, code). */
static bool s_ota_open;                         /* (L4) a write stopped this boot with the C6's OTA session still open */
static int c6_send(const c6_stage2_t *h, volatile int *pct, int32_t *code, uint32_t *off) {
    esp_err_t e = ESP_FAIL;
    if (radio_hosted_up()) {
        ESP_LOGI(TAG, "c6: writing %lu B, v%lu, hosted %lu.%lu", (unsigned long)h->size,
                 (unsigned long)h->relay_version, (unsigned long)radio_hosted_ver().major, (unsigned long)radio_hosted_ver().minor);
        if (s_ota_open) { eh_host_cp_ota_end(); s_ota_open = false; }   /* (L4: hosted has no abort - an end on the unfinished image closes it) */
        e = eh_host_cp_ota_begin();
    }
    *code = e;
    if (e != ESP_OK) { ESP_LOGE(TAG, "c6: begin refused (0x%x)", (unsigned)e); return C6X_BEGIN; }
    s_ota_open = true;
    static uint8_t chunk[C6_CHUNK];
    for (uint32_t o = 0; o < h->size; o += sizeof chunk) {
        const uint32_t n = h->size - o < sizeof chunk ? h->size - o : sizeof chunk;
        if (esp_flash_read(NULL, chunk, C6_STAGE_OFFSET + sizeof *h + o, n) != ESP_OK) { ESP_LOGE(TAG, "c6: read failed at %lu", (unsigned long)o); return C6X_READ; }
        if ((e = eh_host_cp_ota_write(chunk, n)) != ESP_OK) { *code = e; *off = o;
            ESP_LOGE(TAG, "c6: write failed at %lu (0x%x)", (unsigned long)o, (unsigned)e); return C6X_CHUNK; }
        if (pct) *pct = (int)((uint64_t)(o + n) * 100 / h->size);
        vTaskDelay(1);                          /* the tank keeps drawing */
    }
    return C6X_OK;
}
_Static_assert(C6_ERR_INVALID_SIZE == ESP_ERR_INVALID_SIZE, "c6_ho.h: hosted's too-big answer");
_Static_assert(C6_ERR_UNCONFIRMED == ESP_ERR_NOT_ALLOWED, "c6_ho.h: the relay's on-probation refusal (firmware-c6 guard.c)");
/* ... the end, and for a C6 that switches only when told, activate. C6X_OK: switched */
static int c6_finish(bool act, int32_t *code) {
    esp_err_t e = eh_host_cp_ota_end(); s_ota_open = false; *code = e;
    if (e != ESP_OK) { ESP_LOGE(TAG, "c6: the C6 rejected the image (end: 0x%x)", (unsigned)e); return C6X_END; }
    if (act && (*code = e = eh_host_cp_ota_activate()) != ESP_OK) { ESP_LOGW(TAG, "c6: activate: 0x%x", (unsigned)e); return C6X_ACTIVATE; }
    return C6X_OK;
}
/* (M4, esp-hosted-mcu#240) the restart after a write: the C6 held in reset (its enable pin low - it stops driving the
   bus, and its guard is not running while the P4 is away), then the whole P4 reset as Espressif suggested in #240, not
   esp_restart's CPU reset, which leaves the SDMMC host's state behind. The next boot's bring-up releases the C6. */
#ifndef CONFIG_ESP_HOSTED_HOST_RESET_GPIO
#define CONFIG_ESP_HOSTED_HOST_RESET_GPIO (-1)
#endif
static void c6_hold(void) {
    const int pin = CONFIG_ESP_HOSTED_HOST_RESET_GPIO;
    if (pin < 0) return;
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT); gpio_set_level((gpio_num_t)pin, 0);
}
void radio_hosted_restart_clean(void) {
    c6_hold(); vTaskDelay(pdMS_TO_TICKS(50));
    esp_rom_software_reset_system();
    esp_restart();                          /* (not reached) */
}
/* (M4) the function's HOLD: the C6 held in reset for the rest of this boot (#240's stuck bus, most likely), the radio down */
static void c6_hold_now(const char *when) {
    ESP_LOGE(TAG, "c6: %s: no C6 after a restart (#240?) - held", when);
    c6_hold(); radio_hosted_mark_dead("the bus may be stuck after the restart");
}
/* the director's `radio c6 update`: any verified C6 image (the factory stand-in's proof too), then both chips restart.
   A bench path: not the handover's (the tank's own automatic and LINK writes), so not through its record */
static void c6_update(void) {
    c6_stage2_t h; int32_t code; uint32_t off;
    if (!stage_read(&h, true, false) || c6_send(&h, NULL, &code, &off) != C6X_OK) return;
    const radio_fwver_t v = radio_hosted_ver(); const uint8_t hv[3] = { (uint8_t)v.major, (uint8_t)v.minor, 0 };
    greet_mark(true);
    if (c6_finish(c6_ho_activates(hv), &code) != C6X_OK) { greet_mark(false); return; }
    ESP_LOGI(TAG, "c6: written - both chips restart");
    vTaskDelay(pdMS_TO_TICKS(2000));
    radio_hosted_restart_clean();
}

/* (the record's words: the log and radio_hosted_report) */
static const char *const WHY[C6W_N] = { "-", "write cut short", "check cut short", "no SDIO answer", "answers nothing", "start refused", "write failed",
    "too big for its slot", "turned back", "no way back in its slots", "glass busy", "written, not greeted", "in the C6", "out of tries", "not by itself",
    "bus stuck (unplug, plug in)", "in the C6, on probation", "an older record: counted at the cap", "refused twice at the same place",
    "its hosted answers, its relay not" };
static const char *const HO[] = { "idle", "checking", "ready", "writing", "written", "stopped", "not needed", "no stage", "no answer", "not now", "refused", "restarting" };
/* ---- THE HANDOVER (radio_hosted.h; its rules: c6_ho.h) ---- */
static volatile int s_ho_pct;
static bool rec_save(const c6_rec_t *r) {
    nvs_handle_t n; esp_err_t e;
    if ((e = nvs_open("radio", NVS_READWRITE, &n)) == ESP_OK) {
        if ((e = nvs_set_blob(n, "c6rec", r, sizeof *r)) == ESP_OK) e = nvs_commit(n);
        nvs_close(n);
    }
    if (e != ESP_OK) ESP_LOGE(TAG, "c6: record not saved (0x%x)", (unsigned)e);
    return e == ESP_OK;
}
#define HO_READY_WAIT_US (120 * 1000000LL)     /* needed and verified, but the keeper is busy on the glass this long: not now */
/* the check (and the write): on a task of its own, under the radio's lock - the facts gathered, c6_ho_step decides */
static void ho_task(void *arg) {
    (void)arg;
    radio_lock();
    c6_stage2_t h;
    c6_ho_ev_t e = { .type = C6E_STAGE };
    e.ok = stage_read(&h, true, true) && !memcmp(h.sha256, s_hs.r.sha, 8);   /* a good stage of our relay, verified: the one this boot read */
    if (ho_step(&e, false) == C6A_BRING_UP) {
        const bool up = radio_hosted_up();
        nr_state_t st; bool heard = false;      /* three PINGs (the 11-24 review: one lost ping read as "no relay") */
        for (int k = 0; up && k < 3 && !heard; k++) if (!(heard = radio_hosted_ask(NR_MSG_PING, NULL, 0, &st, 2000))) vTaskDelay(pdMS_TO_TICKS(500));
        const radio_fwver_t v = radio_hosted_ver();
        c6_ho_ev_t f = { .type = C6E_HEARD, .up = up, .silent = radio_hosted_why() == RADIO_NO_ANSWER, .st = heard ? &st : NULL,
                         .said = up && radio_hosted_ver_said(), .hv = { (uint8_t)(v.major > 255 ? 255 : v.major), (uint8_t)(v.minor > 255 ? 255 : v.minor), (uint8_t)(v.patch > 255 ? 255 : v.patch) } };
        f.rpc = up && !heard && !f.said && radio_hosted_wifi();   /* (H2) said nothing: does it answer ANY request? (#259) */
        int a = ho_step(&f, false);
        ESP_LOGI(TAG, "c6: relay v%d, stage v%lu: %s%s", heard ? st.version : 0, (unsigned long)h.relay_version, a == C6A_READY ? "to write" : HO[s_hs.ho], heard && st.version >= 3 ? "" : " (no slots)");
        if (a == C6A_PROBE) { const bool got = probe(&st); c6_ho_ev_t p = { .type = C6E_PROBED, .st = got ? &st : NULL }; a = ho_step(&p, false); }
        else if (a == C6A_HOLD) c6_hold_now("the visit after the update");
        if (a == C6A_READY) {                   /* (straight, or after an older relay's probe confirmed it - review 7's L3): wait for a bare glass */
            const int64_t until = esp_timer_get_time() + HO_READY_WAIT_US;
            c6_ho_ev_t w = { .type = C6E_READY_POLL };
            while ((a = ho_step(&w, false)) == C6A_WAIT) { vTaskDelay(pdMS_TO_TICKS(100)); w.late = esp_timer_get_time() > until; }
            if (a == C6A_WRITE) {
                c6_ho_ev_t r = { .type = C6E_RESULT };
                s_ho_pct = 0;
                r.at = (uint8_t)c6_send(&h, &s_ho_pct, &r.code, &r.off);
                if (r.at == C6X_OK) { const c6_ho_ev_t sent = { .type = C6E_SENT }; a = ho_step(&sent, false);
                    r.at = a == C6A_END || a == C6A_END_ACTIVATE ? (uint8_t)c6_finish(a == C6A_END_ACTIVATE, &r.code) : C6X_END; }   /* (not saved: no end) */
                ho_step(&r, false);
                ESP_LOGI(TAG, "c6: write %s, %u/%d", HO[s_hs.ho], s_hs.r.tries, C6_TRIES_ALL);
            }
        }
    }
    radio_unlock();
    vTaskDelete(NULL);
}
static void ho_start(int a) {                  /* C6A_CHECK: the check's task */
    if ((a == C6A_CHECK || a == C6A_CHECK_WAIT || a == C6A_CHECK_ESC) && xTaskCreate(ho_task, "pt_c6_hand", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "c6: no room for the handover's task");
        const c6_ho_ev_t e = { .type = C6E_STAGE, .ok = 2 }; ho_step(&e, false);   /* (no room: not the stage's fault - the next boot) */
    }
}
int radio_hosted_ho_tick(bool bare, bool idle, bool flow, bool card) {
    const c6_ho_ev_t e = { .type = C6E_TICK, .ok = bare, .idle = idle, .flow = flow, .card = card,
                           .now_s = (uint32_t)(esp_timer_get_time() / 1000000) };
    const int a = ho_step(&e, true);
    ho_start(a);
    return a;
}
int radio_hosted_ho_link(void) { const c6_ho_ev_t e = { .type = C6E_LINK }; const int a = ho_step(&e, false); ho_start(a); return a; }
int radio_hosted_ho_link_failed(void) { const c6_ho_ev_t e = { .type = C6E_LINK_FAILED }; return ho_step(&e, false); }
int radio_hosted_ho_esc_abort(void) { const c6_ho_ev_t e = { .type = C6E_ESC_ABORT }; return ho_step(&e, false); }
int radio_hosted_handover_state(int *pct) { if (pct) *pct = s_ho_pct; return s_hs.ho; }
static void scan(void) {
    if (!radio_hosted_wifi()) return;   /* (left up afterwards: radio_hosted.h) */
    esp_err_t e;
    if ((e = esp_wifi_scan_start(NULL, true)) != ESP_OK) ESP_LOGE(TAG, "scan: 0x%x", (unsigned)e);
    else {
        static wifi_ap_record_t ap[12];
        uint16_t n = 12;
        if (esp_wifi_scan_get_ap_records(&n, ap) == ESP_OK) {
            ESP_LOGI(TAG, "scan: %u networks heard through the C6", (unsigned)n);
            for (int i = 0; i < n; i++) ESP_LOGI(TAG, "  ch %2d  %4d dBm  %s", ap[i].primary, ap[i].rssi, (const char *)ap[i].ssid);
        }
    }
}

/* THE RADIO REPORT (the one-install audit's H2): the chip as last seen, kept across restarts in NVS - no bring-up, so the
   tank check on pocketank.com/help (it sends `state`) reads it on any tank. Two or three "radio report:" lines. */
void radio_hosted_report(void) {
    const c6_rec_t *r = &s_hs.r;
    if (!s_hs.size) { ESP_LOGI(TAG, "radio report: no stage"); return; }
    ESP_LOGI(TAG, "radio report: stage relay v%u %02x%02x%02x%02x %s | hosted %u.%u.%u%s | relay v%u%s", s_hs.rv, r->sha[0], r->sha[1], r->sha[2],
             r->sha[3], r->ok ? "IN THE C6" : r->rep.checks ? "not in the C6" : "never checked", r->rep.hv[0], r->rep.hv[1], r->rep.hv[2], r->rep.hv_said ? "" : " (did not say)", r->rep.rv, r->rep.heard ? "" : " (no relay)");
    ESP_LOGI(TAG, "radio report: last %s, %s | writes %u/%d (auto %u/%d), %d left | back %u quiet %u checks %u%s%s%s | deaf %u/%d code 0x%02x%s", r->rep.last < sizeof HO / sizeof HO[0] ? HO[r->rep.last] : "?",
             r->rep.why < C6W_N ? WHY[r->rep.why] : "?", r->tries, C6_TRIES_ALL, r->auto_n, C6_TRIES_AUTO, c6_tries_left(r), r->backs, r->quiet, r->rep.checks, r->cut ? " cut" : "", r->never ? " never" : "", r->seen ? " probation" : "",
             r->deaf, C6_DEAF_BOOTS, r->fail_code, s_hs.nosave ? " nvs" : "");
    if (r->rep.slots & NR_SLOT_REPORT)
        ESP_LOGI(TAG, "radio report: slots run 0x%02x next 0x%02x %u KB%s, %u ota%s, %s", r->rep.run_sub, r->rep.next_sub, r->rep.next_kb, r->rep.slots & NR_SLOT_NEXT_APP ? " (an app)" : "",
                 r->rep.slots & NR_SLOT_OTA_N, r->rep.slots & NR_SLOT_FACTORY ? " + factory" : "", r->rep.slots & NR_SLOT_BACK_OK ? "a way back" : "NO WAY BACK");
}

static void status(void);
/* the director's commands that wait on the C6 run on a radio task of the console's, never on the tank task (the 11-24
   review: `radio` on a silent C6 waited 25-30 s there, past the tank's 20 s watchdog). Internal RAM for its stack: the
   C6 update reads the P4's flash. One at a time; made for the command and gone after it. */
enum { CMD_STATUS, CMD_SCAN, CMD_UPDATE, CMD_STAGE };
static volatile bool s_cmd_busy;
static void cmd_task(void *arg) {
    const int what = (int)(intptr_t)arg;
    radio_lock();
    if (what == CMD_SCAN) scan(); else if (what == CMD_UPDATE) c6_update();
    else if (what == CMD_STAGE) { c6_stage2_t h;     /* (reads 1.5 MB: here, under the lock, not on the tank task) */
        if (stage_read(&h, true, false)) ESP_LOGI(TAG, "c6: staged: %lu B, relay v%lu, crc+sha ok", (unsigned long)h.size, (unsigned long)h.relay_version); }
    else status();
    radio_unlock();
    s_cmd_busy = false; vTaskDelete(NULL);
}
static void cmd_run(int what) {
    if (s_cmd_busy) { ESP_LOGW(TAG, "radio: the last command is still running"); return; }
    s_cmd_busy = true;
    if (xTaskCreate(cmd_task, "pt_radio_cmd", 6144, (void *)(intptr_t)what, 3, NULL) != pdPASS) { s_cmd_busy = false; ESP_LOGE(TAG, "radio: no room for the command's task"); }
}
void radio_hosted_cmd(tank_t *t, int argc, char **argv) {
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "scan")) { cmd_run(CMD_SCAN); return; }
    if (!strcmp(c, "c6")) {
        if (argc > 2 && !strcmp(argv[2], "update")) { if (t) progression_save(t); cmd_run(CMD_UPDATE); return; }
        if (argc > 2 && !strcmp(argv[2], "forget")) {   /* (bench only) the handover's record gone: the next boot starts it afresh - and with
                                                   it EVERY bound the record kept (tries, turn-backs, refusals, chains: review 10's L1); restart at once (this boot saves its RAM copy over it). Never on a keeper's tank */
            nvs_handle_t n; if (nvs_open("radio", NVS_READWRITE, &n) == ESP_OK) { nvs_erase_key(n, "c6rec"); nvs_commit(n); nvs_close(n); }
            ESP_LOGW(TAG, "c6: record gone"); return; }
        cmd_run(CMD_STAGE);
        return;
    }
    if (!strcmp(c, "report")) { radio_hosted_report(); return; }
    if (!strcmp(c, "mem")) { radio_mem_log(radio_hosted_alive() ? "now (the stack is up)" : "now (the stack is down)"); return; }
    if (c[0]) { ESP_LOGW(TAG, "radio | radio report (as last seen, from NVS) | radio mem (the RAM the radio sees) | radio scan (networks heard through the C6) | radio c6 (the staged C6 image) | radio c6 update (write it into the C6)"); return; }
    cmd_run(CMD_STATUS);
}
static void status(void) {
    if (!radio_hosted_up()) return;
    nr_state_t st;
    if (radio_hosted_ask(NR_MSG_PING, NULL, 0, &st, 1500))
        ESP_LOGI(TAG, "C6: relay v%d (%s), ESP-NOW %s, ch %d, %02x:%02x:%02x:%02x:%02x:%02x | rx %lu tx %lu nak %lu drop %lu",
                 st.version, st.guard_s == 0 ? "confirmed" : st.version >= 2 ? "new: on probation until its radio sends (LINK, or the greeting after an update)" : "new: this ask just confirmed it", st.up ? "up" : "down", st.channel,
                 st.mac[0], st.mac[1], st.mac[2], st.mac[3], st.mac[4], st.mac[5],
                 (unsigned long)st.rx, (unsigned long)st.tx, (unsigned long)st.tx_fail, (unsigned long)st.dropped);
    else ESP_LOGI(TAG, "C6: no relay, hosted %lu.%lu.%lu",
                  (unsigned long)radio_hosted_ver().major, (unsigned long)radio_hosted_ver().minor, (unsigned long)radio_hosted_ver().patch);
}
