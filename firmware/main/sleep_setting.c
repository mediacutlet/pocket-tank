/* sleep_setting.c - see sleep_setting.h. */
#include "sleep_setting.h"
#include "render.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#ifdef SLEEP_SETTING_ROW
static const char *TAG = "sleep";
/* the build's Kconfig mode, the factory default: what the tank does until
   the keeper picks a segment */
#if CONFIG_POCKET_TANK_SLEEP_NONE
#define SLEEP_SETTING_DEFAULT SET_SLEEP_NEVER
#elif CONFIG_POCKET_TANK_SLEEP_SCREEN
#define SLEEP_SETTING_DEFAULT SET_SLEEP_SCREEN
#else
#define SLEEP_SETTING_DEFAULT SET_SLEEP_LIGHT
#endif
static int s_choice = SLEEP_SETTING_DEFAULT;
static bool s_saved;                     /* s_choice came from NVS or the row, not from the build */

static bool valid(int choice) {
    if (choice == SET_SLEEP_NEVER || choice == SET_SLEEP_SCREEN || choice == SET_SLEEP_LIGHT) return true;
    return false;
}
void sleep_setting_init(void) {
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return;
    /* a value outside the row (written by a later build, or garbage) is no
       choice at all: the build's default stands, and says so in the boot log */
    if (nvs_get_u8(h, "sleep", &v) == ESP_OK && valid(v)) { s_choice = v; s_saved = true; }
    nvs_close(h);
}
void sleep_setting_save(void) {
    /* only a choice the keeper made: the build's default is not written, so
       a later build with another default still brings its own */
    if (!s_saved) return;
    nvs_handle_t h;
    if (nvs_open("tank", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "sleep", (uint8_t)s_choice); nvs_commit(h); nvs_close(h);
}
int sleep_setting(void) { return s_choice; }
bool sleep_setting_saved(void) { return s_saved; }
void sleep_setting_set(int choice) {
    if (!valid(choice)) return;
    s_choice = choice; s_saved = true; sleep_setting_save();
    if (choice == SET_SLEEP_NEVER) ESP_LOGI(TAG, "NEVER: every way into sleep is ignored, the tank never goes dark");
    else if (choice == SET_SLEEP_SCREEN) ESP_LOGI(TAG, "SCREEN: a sleep goes dark, the chip awake between looks");
    else ESP_LOGI(TAG, "LIGHT: a sleep goes dark, light-sleeping between looks (awake while a USB host is attached)");
}
#else
/* no row (an AMOLED board, or a 320x240 board built for deepsleep): the build decides */
void sleep_setting_init(void) { }
void sleep_setting_save(void) { }
int sleep_setting(void) { return -1; }
bool sleep_setting_saved(void) { return false; }
void sleep_setting_set(int choice) { (void)choice; }
#endif
