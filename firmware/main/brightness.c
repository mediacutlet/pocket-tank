#include "brightness.h"
#include "display_port.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "brightness";
static int s_pct = 100, s_applied = -1;

void brightness_init(void) {
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u8(h, "bright", &v) == ESP_OK && v >= 10 && v <= 100) s_pct = ((v + 5) / 10) * 10;   /* ten steps since 2026-10-10 (a saved 15 reads as 20) */
    nvs_close(h);
}
void brightness_save(void) {
    nvs_handle_t h;
    if (nvs_open("tank", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bright", (uint8_t)s_pct); nvs_commit(h); nvs_close(h);
}
int brightness_level(void) { return s_pct; }
bool brightness_set_level(int pct) {
    if (pct < 10 || pct > 100 || pct % 10) return false;   /* 10 .. 100 by tens (2026-10-10 evening; 15/30/60/100 before) */
    s_pct = pct; brightness_save(); s_applied = -1;
    ESP_LOGI(TAG, "level %d%%", s_pct);
    return true;
}
void brightness_cycle(void) { brightness_set_level(s_pct >= 100 ? 10 : s_pct + 10); }
void brightness_apply(bool night) {
    int target = s_pct * 255 / 100;
    if (night) target = target * 6 / 10;
    if (target != s_applied) { display_port_set_brightness((uint8_t)target); s_applied = target; }
}
