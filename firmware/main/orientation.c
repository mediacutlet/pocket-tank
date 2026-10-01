/* orientation.c - see orientation.h. */
#include "orientation.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "orientation";
static bool s_flipped;
static bool s_face_sleep = true;    /* the gesture is on until the keeper says otherwise */

void orientation_init(void) {
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u8(h, "flip", &v) == ESP_OK) s_flipped = v != 0;
    if (nvs_get_u8(h, "facedn", &v) == ESP_OK) s_face_sleep = v != 0;
    nvs_close(h);
}
void orientation_save(void) {
    nvs_handle_t h;
    if (nvs_open("tank", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "flip", s_flipped ? 1 : 0);
    nvs_set_u8(h, "facedn", s_face_sleep ? 1 : 0);
    nvs_commit(h); nvs_close(h);
}
bool orientation_flipped(void) { return s_flipped; }
bool orientation_face_sleep(void) { return s_face_sleep; }
void orientation_set_face_sleep(bool on) {
    s_face_sleep = on; orientation_save();
    ESP_LOGI(TAG, "face down %s", s_face_sleep ? "SLEEPS the tank" : "is IGNORED");
}
void orientation_set(bool flipped) {
    s_flipped = flipped; orientation_save();
    ESP_LOGI(TAG, "screen %s", s_flipped ? "FLIPPED" : "UPRIGHT");
}
