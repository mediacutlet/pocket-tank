#include "brightness.h"
#include "display_port.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "brightness";
static int s_pct = 100, s_applied = -1;
static int s_saved = 100;   /* the level last read from or written to NVS (brightness_restore) */

static bool level_ok(int pct) { return pct >= 5 && pct <= 100 && pct % 5 == 0; }   /* the slider's steps (0.3.9; 30 / 60 / 100 before) */

void brightness_init(void) {
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u8(h, "bright", &v) == ESP_OK && level_ok(v)) s_pct = v;
    nvs_close(h);
    s_saved = s_pct;
}
void brightness_save(void) {
    s_saved = s_pct;
    nvs_handle_t h;
    if (nvs_open("tank", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bright", (uint8_t)s_pct); nvs_commit(h); nvs_close(h);
}
int brightness_level(void) { return s_pct; }
bool brightness_set_level(int pct) {
    if (!level_ok(pct)) return false;
    s_pct = pct; brightness_save(); s_applied = -1;
    ESP_LOGI(TAG, "level %d%%", s_pct);
    return true;
}
void brightness_preview(int pct) { if (level_ok(pct)) { s_pct = pct; s_applied = -1; } }
void brightness_restore(void) { s_pct = s_saved; s_applied = -1; }   /* a preview undone: the saved level back, nothing written */
void brightness_cycle(void) { brightness_set_level(s_pct == 100 ? 60 : s_pct == 60 ? 30 : 100); }
void brightness_apply(bool night) {
    int target = s_pct * 255 / 100;
    if (night) target = target * 6 / 10;
    if (target != s_applied) { display_port_set_brightness((uint8_t)target); s_applied = target; }
}
