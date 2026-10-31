/* persist_port_esp.c — progression ports on the device: NVS blob + wall clock.
 * Wall clock: esp time (set from the PCF85063 RTC at boot in Track 4; until
 * then it is 0 on a cold boot, which simply disables the ravenous rule). */
#include "progression.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "persist";

/* A save LONGER than this build's save_t is a newer build's (a rollback, or
 * the installer's older release): its head is our whole layout (tails only
 * ever append), so the head is loaded and the newer tail - things this build
 * never knew about - reads as its defaults when the newer build comes back.
 * A blob that exists but won't read blocks every save until a reset, so a
 * tank we could not load is never overwritten by a fresh one. */
static bool s_save_blocked;
bool persist_port_load(void *buf, size_t max, size_t *got) {
    nvs_handle_t h; if (nvs_open(SAVE_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0; esp_err_t e = nvs_get_blob(h, SAVE_NVS_KEY, NULL, &len);   /* the stored length first */
    if (e == ESP_OK && len == 0) e = ESP_ERR_NVS_NOT_FOUND;         /* an empty blob is nothing saved */
    if (e == ESP_OK && len <= max) e = nvs_get_blob(h, SAVE_NVS_KEY, buf, &len);
    else if (e == ESP_OK) {                                /* a newer build's save: keep its head */
        uint8_t *all = malloc(len);
        if (!all) e = ESP_ERR_NO_MEM;
        else if ((e = nvs_get_blob(h, SAVE_NVS_KEY, all, &len)) == ESP_OK) {
            memcpy(buf, all, max);
            ESP_LOGW(TAG, "a newer build's save (%u bytes): loading the first %u", (unsigned)len, (unsigned)max);
            len = max;
        }
        free(all);
    }
    nvs_close(h);
    if (e == ESP_OK) *got = len;
    else if (e != ESP_ERR_NVS_NOT_FOUND) {
        s_save_blocked = true;
        ESP_LOGE(TAG, "load failed: %s (%u bytes) - saving is OFF so the stored tank stays", esp_err_to_name(e), (unsigned)len);
    }
    return e == ESP_OK;
}
bool persist_port_save(const void *buf, size_t len) {
    if (s_save_blocked) return false;
    nvs_handle_t h; if (nvs_open(SAVE_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, SAVE_NVS_KEY, buf, len); if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h); if (e != ESP_OK) ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(e));
    return e == ESP_OK;
}
/* the keeper's reset: the tank goes - "save" and the director's parked "bk"
 * alike, so nothing can bring it back. Since the tank switcher (D13) the
 * board's own keys in the namespace stay: brightness, volume, the battery's
 * size, the touch fit, the bedtime log. (It was nvs_erase_all until 0.3.9.3.) */
bool persist_port_erase(void) {
    nvs_handle_t h; if (nvs_open(SAVE_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_erase_key(h, SAVE_NVS_KEY); if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    esp_err_t e2 = nvs_erase_key(h, "bk"); if (e == ESP_OK && e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) e = e2;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGW(TAG, "erase failed: %s", esp_err_to_name(e));
    else { ESP_LOGI(TAG, "the tank on the glass erased (the shelf and the board's settings kept)"); s_save_blocked = false; }
    return e == ESP_OK;
}
int64_t clock_port_now_unix(void) {
    time_t now = time(NULL); return now > 1700000000 ? (int64_t)now : 0;   /* 0 until the RTC sets it */
}

/* ---- the shelf's ports (the tank switcher, common/shelf.h): the `storage`
 * partition past its first 64 KB (the NVS rescue copy, main.c nvs_start,
 * sits at 0 and must never be written over) and the NVS namespace `frame`.
 * A `tank` reset (persist_port_erase) never touches either: the reset is the
 * tank on the glass only (D13). ---- */
#include "shelf.h"
#include "esp_partition.h"
#define SHELF_BASE 0x10000
static const esp_partition_t *shelf_part(void) {
    static const esp_partition_t *p; static bool looked;
    if (!looked) { p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage"); looked = true; }
    return p;
}
size_t shelf_port_size(void) { const esp_partition_t *p = shelf_part(); return p && p->size > SHELF_BASE ? p->size - SHELF_BASE : 0; }
bool shelf_port_read(uint32_t off, void *buf, size_t n) { const esp_partition_t *p = shelf_part(); return p && esp_partition_read(p, SHELF_BASE + off, buf, n) == ESP_OK; }
bool shelf_port_write(uint32_t off, const void *buf, size_t n) { const esp_partition_t *p = shelf_part(); return p && esp_partition_write(p, SHELF_BASE + off, buf, n) == ESP_OK; }
bool shelf_port_erase(uint32_t off, size_t n) { const esp_partition_t *p = shelf_part(); return p && esp_partition_erase_range(p, SHELF_BASE + off, n) == ESP_OK; }
bool shelf_port_frame_get(const char *key, void *buf, size_t max, size_t *got) {
    nvs_handle_t h; if (nvs_open("frame", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = max; esp_err_t e = nvs_get_blob(h, key, buf, &len);
    nvs_close(h);
    if (e == ESP_OK) *got = len;
    return e == ESP_OK;
}
bool shelf_port_frame_set(const char *key, const void *buf, size_t n) {
    nvs_handle_t h; if (nvs_open("frame", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, key, buf, n); if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h); if (e != ESP_OK) ESP_LOGW(TAG, "frame %s: %s", key, esp_err_to_name(e));
    return e == ESP_OK;
}
bool shelf_port_frame_del(const char *key) {
    nvs_handle_t h; if (nvs_open("frame", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_erase_key(h, key); if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND;
}
bool persist_port_forget(void) {                  /* the save on the glass only: a fresh tank comes in */
    nvs_handle_t h; if (nvs_open(SAVE_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_erase_key(h, SAVE_NVS_KEY); if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND;
}
