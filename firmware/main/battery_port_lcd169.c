/* battery_port_lcd169.c — the ESP32-S3-(Touch-)LCD-1.69's power side, behind
 * the same battery_port.h the AXP2101 board uses. There is no PMIC here:
 *   - the gauge is VBAT through a 200k/100k divider on GPIO1 (ADC1 ch0),
 *     turned into a percentage with a resting Li-ion curve (so it is an
 *     estimate - it reads high while charging, a little low under load);
 *   - "the cable" is the ESP32-S3's own USB: the USB-Serial-JTAG sees the
 *     host's start-of-frame packets. A wall charger with no data lines looks
 *     like "on battery" (the pill just shows the voltage-based level);
 *   - the PWR key is SYS_OUT (GPIO40, low while pressed), a plain GPIO polled
 *     from the tank task: released < 1.5 s = short press (sleep), held 1.5 s
 *     = long press (power-off);
 *   - power-off is SYS_EN (GPIO41) low: the battery switch opens and the board
 *     dies - pressing PWR switches it back on and the tank cold-boots. On USB
 *     the board stays powered, so main.c falls back to deep sleep. */
#include "battery_port.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_ok;
static int64_t s_last_us = -1;
static int s_mv;                                /* filtered VBAT */
static float s_frac; static int s_state = BAT_ON_BATTERY; static bool s_valid;

/* resting Li-ion open-circuit curve, mV at 0,10..100 % */
static const int OCV[11] = { 3300, 3600, 3690, 3740, 3770, 3800, 3850, 3920, 4000, 4080, 4180 };
static float mv_to_frac(int mv) {
    if (mv <= OCV[0]) return 0;
    if (mv >= OCV[10]) return 1;
    for (int i = 1; i <= 10; i++)
        if (mv < OCV[i]) return ((i - 1) + (float)(mv - OCV[i - 1]) / (OCV[i] - OCV[i - 1])) / 10.0f;
    return 1;
}

static int read_mv_raw(void) {
    if (!s_adc) return 0;
    int sum = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw) != ESP_OK) continue;
        if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) sum += mv;
        else sum += raw * 3100 / 4095;          /* uncalibrated: 12 dB range ~3.1 V */
        n++;
    }
    return n ? sum * 3 / n : 0;                 /* the divider: x3 */
}

/* SYS_EN first thing in app_main: on battery the board only stays on while
 * the PWR key is held until this pin goes high - and the hold keeps it high
 * through a deep sleep (isolated pads would float it and drop the power). */
void board_power_hold(void) {
    gpio_hold_dis(PIN_SYS_EN);
    gpio_config_t en = { .pin_bit_mask = 1ULL << PIN_SYS_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&en);
    gpio_set_level(PIN_SYS_EN, 1);
    gpio_hold_en(PIN_SYS_EN);
    gpio_config_t key = { .pin_bit_mask = 1ULL << PIN_SYS_OUT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&key);
}

bool battery_port_init(i2c_master_bus_handle_t bus) {
    (void)bus;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { s_adc = NULL; ESP_LOGW(TAG, "ADC unavailable: no gauge"); return true; }
    adc_oneshot_chan_cfg_t ccfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &ccfg);
    adc_cali_curve_fitting_config_t cc = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
                                           .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cc, &s_cali) != ESP_OK) s_cali = NULL;
    s_mv = read_mv_raw();
    s_ok = true;
    ESP_LOGI(TAG, "LCD-1.69 power: VBAT %d mV by ADC%s, PWR key on GPIO%d, SYS_EN held high", s_mv,
             s_cali ? " (calibrated)" : "", PIN_SYS_OUT);
    return true;                                /* "a PMIC": the PWR key exists and power-off is real (on battery) */
}

static void refresh(void) {
    int64_t now = esp_timer_get_time();
    if (s_last_us >= 0 && now - s_last_us < 1000000) return;
    s_last_us = now;
    int mv = read_mv_raw();
    s_mv = s_mv ? (s_mv * 3 + mv) / 4 : mv;     /* light smoothing: the ADC wobbles tens of mV */
    bool usb = usb_serial_jtag_is_connected();
    s_valid = s_ok && s_mv > 2800 && s_mv < 4500;   /* outside that: no cell on the connector */
    s_frac = mv_to_frac(s_mv);
    s_state = !usb ? BAT_ON_BATTERY : s_mv >= 4150 ? BAT_FULL : BAT_CHARGING;
}

bool battery_port_read(float *frac, bool *charging) {
    refresh();
    if (!s_valid) return false;
    if (frac) *frac = s_frac;
    if (charging) *charging = s_state == BAT_CHARGING;
    return true;
}
int battery_port_state(void) { refresh(); return s_state; }
int battery_port_vbat_mv(void) { refresh(); return s_valid ? s_mv : 0; }

bool battery_port_poweroff(void) {
    ESP_LOGI(TAG, "SYS_EN low: the battery switch opens (on USB the board stays up)");
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_hold_dis(PIN_SYS_EN);
    gpio_set_level(PIN_SYS_EN, 0);
    return true;
}

/* ---- the PWR key: SYS_OUT, low while pressed ---- */
#define KEY_LONG_US     1500000
#define KEY_DEBOUNCE_US 30000
static bool s_armed;                    /* seen released since boot: the power-on press never counts */
static int64_t s_down_us; static bool s_long_fired;

void battery_port_key_init(void) { s_armed = gpio_get_level(PIN_SYS_OUT) != 0; s_down_us = 0; s_long_fired = false; }

int battery_port_key_poll(void) {
    int64_t now = esp_timer_get_time();
    bool down = gpio_get_level(PIN_SYS_OUT) == 0;
    if (down) {
        if (!s_down_us) s_down_us = now;
        if (s_armed && !s_long_fired && now - s_down_us >= KEY_LONG_US) { s_long_fired = true; return 2; }
        return 0;
    }
    int r = 0;
    if (s_down_us && s_armed && !s_long_fired && now - s_down_us >= KEY_DEBOUNCE_US) r = 1;
    s_down_us = 0; s_long_fired = false; s_armed = true;
    return r;
}

void battery_port_key_trace(int seconds) {
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000, down = 0; int n = 0;
    ESP_LOGI(TAG, "key trace: press PWR a few times in the next %d s", seconds);
    bool was = gpio_get_level(PIN_SYS_OUT) == 0;
    while (esp_timer_get_time() < end) {
        bool d = gpio_get_level(PIN_SYS_OUT) == 0; int64_t now = esp_timer_get_time();
        if (d && !was) down = now;
        if (!d && was && down) ESP_LOGI(TAG, "key trace: press %d held %d ms", ++n, (int)((now - down) / 1000));
        was = d;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    s_down_us = 0; s_long_fired = false; s_armed = gpio_get_level(PIN_SYS_OUT) != 0;
}

void battery_port_dump(void) {
    refresh();
    ESP_LOGI(TAG, "LCD-1.69: VBAT %d mV (%s), ~%.0f%%, USB %s, SYS_EN %d, PWR key %s", s_mv, s_valid ? "cell present" : "no cell?",
             s_frac * 100, usb_serial_jtag_is_connected() ? "connected" : "not seen", gpio_get_level(PIN_SYS_EN),
             gpio_get_level(PIN_SYS_OUT) ? "up" : "DOWN");
}
bool battery_port_set_rail(const char *name, bool on) { (void)name; (void)on; ESP_LOGW(TAG, "no PMIC rails on this board"); return false; }
void battery_port_trim_rails(void) {}
