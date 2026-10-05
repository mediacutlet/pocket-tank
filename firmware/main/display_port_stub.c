/* display_port_stub.c — no panel (QEMU / compile-only). Counts frames so the
 * render loop is exercised end to end and reports fps in the log. */
#include "display_port.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "display";
static uint32_t frames = 0;
static int64_t last_report = 0;

bool display_port_init(void) { ESP_LOGI(TAG, "stub display port (no panel)"); return true; }
void display_port_sleep(void) {}
void display_port_wake(void) {}
void display_port_set_inverted(bool inverted) { (void)inverted; }
static uint8_t s_brightness = 0xFF;
void display_port_set_brightness(uint8_t level) { s_brightness = level; }
uint8_t display_port_brightness(void) { return s_brightness; }

bool board_is_round(void) { return false; }
void display_port_deep_sleep_pins(bool tp_awake_high) { (void)tp_awake_high; }
void display_port_deep_standby(void) { }
void display_port_deep_sleep_bus(void) { }
bool board_has_expander(void) { return true; }
bool board_is_watch(void) { return false; }
bool board_is_sq216(void) { return false; }
void display_port_set_rotation(int quarter) { (void)quarter; }
int  board_round_panel(void) { return 466; }
int  board_pwr_sense_pin(void) { return -1; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FIT; }
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) { *tx = (float)px; *ty = (float)py; }

void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) { *wait_us = *send_us = 0; }
void display_port_flush(const uint16_t *fb) {
    (void)fb;
    frames++;
    int64_t now = esp_timer_get_time();
    if (now - last_report > 5 * 1000000) {
        if (last_report) ESP_LOGI(TAG, "render %.1f fps", frames / ((now - last_report) / 1e6));
        frames = 0; last_report = now;
    }
}
