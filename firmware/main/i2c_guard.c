/* i2c_guard.c - THE I2C BUS GUARD (i2c_guard.h has the why). The wrapped driver calls are named in this component's
 * CMakeLists (-Wl,--wrap=...). */
#include "i2c_guard.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "i2c";

esp_err_t __real_i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn, int ms);
esp_err_t __real_i2c_master_multi_buffer_transmit(i2c_master_dev_handle_t d, i2c_master_transmit_multi_buffer_info_t *b, size_t n, int ms);
esp_err_t __real_i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn, uint8_t *r, size_t rn, int ms);
esp_err_t __real_i2c_master_receive(i2c_master_dev_handle_t d, uint8_t *r, size_t rn, int ms);
esp_err_t __real_i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t addr, int ms);
esp_err_t __real_i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *ret);
esp_err_t __real_i2c_master_bus_rm_device(i2c_master_dev_handle_t d);

static StaticSemaphore_t s_mx_buf;
static SemaphoreHandle_t s_mx;
static int s_sda = -1, s_scl = -1;
static i2c_master_bus_handle_t s_bus;
static bool s_boot_stuck, s_boot_freed;
static uint8_t s_prev_inflight; static uint16_t s_prev_fails;

/* the handle -> address map (the driver keeps the address private) */
#define MAP_N 16
static struct { i2c_master_dev_handle_t h; uint8_t addr; } s_map[MAP_N];
/* the failures by address (0: a handle not in the map) */
#define ST_N 12
static struct { uint8_t addr; uint16_t fail, tmo; } s_st[ST_N];
static uint32_t s_fails, s_recov; static uint8_t s_last; static int s_run; static int64_t s_last_rec_us = -I2C_GUARD_GAP_US;

/* the run's breadcrumb, in RTC memory (kept through every reset but a power loss): the address of a transaction in
   flight, and the failures so far - what a watchdog reset left in the middle of. A check word, so a power-on's
   garbage is never read as an address */
RTC_NOINIT_ATTR static uint32_t s_rtc_word, s_rtc_check;
#define RTC_TAG 0xA5u
static inline void rtc_put(uint8_t inflight) {
    uint32_t w = ((uint32_t)(s_fails > 0xFFFF ? 0xFFFF : s_fails) << 16) | ((uint32_t)inflight << 8) | RTC_TAG;
    s_rtc_word = w; s_rtc_check = ~w;
}

void i2c_guard_lock(void)   { if (s_mx) xSemaphoreTakeRecursive(s_mx, portMAX_DELAY); }
/* a transaction's own wait for the bus: its timeout, as the driver's bus lock waited before the guard (-1: for good) */
static bool lock_for(int ms) {
    if (!s_mx) return true;
    return xSemaphoreTakeRecursive(s_mx, ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(ms) + 1) == pdTRUE;
}
void i2c_guard_unlock(void) { if (s_mx) xSemaphoreGiveRecursive(s_mx); }

static uint8_t addr_of(i2c_master_dev_handle_t d) {
    for (int i = 0; i < MAP_N; i++) if (s_map[i].h == d) return s_map[i].addr;
    return 0;
}
static int line(int pin) { return pin < 0 ? 1 : gpio_get_level(pin); }

static bool recover_locked(const char *why) {
    const int sda0 = line(s_sda), scl0 = line(s_scl);
    s_last_rec_us = esp_timer_get_time(); s_recov++;
    esp_err_t e = s_bus ? i2c_master_bus_reset(s_bus) : ESP_ERR_INVALID_STATE;
    esp_rom_delay_us(20);
    const int sda1 = line(s_sda), scl1 = line(s_scl);
    const bool pmic = s_bus && __real_i2c_master_probe(s_bus, 0x34, 50) == ESP_OK;
    if (pmic) s_run = 0;
    ESP_LOGW(TAG, "I2C BUS RECOVERY %lu (%s): before SDA %s SCL %s | the bus reset %s | after SDA %s SCL %s | the PMIC %s | %lu failures so far, the last at 0x%02x",
             (unsigned long)s_recov, why, sda0 ? "high" : "LOW", scl0 ? "high" : "LOW", e == ESP_OK ? "ran" : esp_err_to_name(e),
             sda1 ? "high" : "LOW", scl1 ? "high" : "LOW", pmic ? "answers" : "DOES NOT ANSWER", (unsigned long)s_fails, s_last);
    return pmic;
}
bool i2c_guard_recover(const char *why) {
    i2c_guard_lock();
    bool ok = recover_locked(why);
    i2c_guard_unlock();
    return ok;
}

static void begin(uint8_t a) { rtc_put(a); }
static esp_err_t end(uint8_t a, esp_err_t e, bool probe) {
    if (e == ESP_OK || (probe && e == ESP_ERR_NOT_FOUND)) {      /* a probe that found nobody: an answer, not a fault */
        s_run = 0; rtc_put(0);
        return e;
    }
    s_fails++; s_last = a; s_run++;
    int k = 0;
    while (k < ST_N && s_st[k].addr != a && (s_st[k].fail || s_st[k].tmo)) k++;
    if (k < ST_N) { s_st[k].addr = a; if (e == ESP_ERR_TIMEOUT) s_st[k].tmo++; else s_st[k].fail++; }
    rtc_put(0);
    const bool sda_low = !line(s_sda), scl_low = !line(s_scl);   /* (the lock is held and the transaction is over: the bus is idle) */
    if (i2c_guard_should_recover(sda_low, scl_low, s_run, esp_timer_get_time() - s_last_rec_us))
        recover_locked(sda_low ? "SDA held low" : scl_low ? "SCL held low" : "failures in a row");
    return e;
}

esp_err_t __wrap_i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn, int ms) {
    if (!lock_for(ms)) return ESP_ERR_TIMEOUT;
    const uint8_t a = addr_of(d); begin(a);
    esp_err_t e = end(a, __real_i2c_master_transmit(d, w, wn, ms), false);
    i2c_guard_unlock(); return e;
}
esp_err_t __wrap_i2c_master_multi_buffer_transmit(i2c_master_dev_handle_t d, i2c_master_transmit_multi_buffer_info_t *b, size_t n, int ms) {
    if (!lock_for(ms)) return ESP_ERR_TIMEOUT;
    const uint8_t a = addr_of(d); begin(a);
    esp_err_t e = end(a, __real_i2c_master_multi_buffer_transmit(d, b, n, ms), false);
    i2c_guard_unlock(); return e;
}
esp_err_t __wrap_i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *w, size_t wn, uint8_t *r, size_t rn, int ms) {
    if (!lock_for(ms)) return ESP_ERR_TIMEOUT;
    const uint8_t a = addr_of(d); begin(a);
    esp_err_t e = end(a, __real_i2c_master_transmit_receive(d, w, wn, r, rn, ms), false);
    i2c_guard_unlock(); return e;
}
esp_err_t __wrap_i2c_master_receive(i2c_master_dev_handle_t d, uint8_t *r, size_t rn, int ms) {
    if (!lock_for(ms)) return ESP_ERR_TIMEOUT;
    const uint8_t a = addr_of(d); begin(a);
    esp_err_t e = end(a, __real_i2c_master_receive(d, r, rn, ms), false);
    i2c_guard_unlock(); return e;
}
esp_err_t __wrap_i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t addr, int ms) {
    if (!lock_for(ms)) return ESP_ERR_TIMEOUT;
    if (!s_bus) s_bus = bus;
    begin((uint8_t)addr);
    esp_err_t e = end((uint8_t)addr, __real_i2c_master_probe(bus, addr, ms), true);
    i2c_guard_unlock(); return e;
}
esp_err_t __wrap_i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *ret) {
    i2c_guard_lock();
    if (!s_bus) s_bus = bus;
    esp_err_t e = __real_i2c_master_bus_add_device(bus, cfg, ret);
    if (e == ESP_OK && ret && cfg) {
        int i = 0;
        while (i < MAP_N && s_map[i].h) i++;
        if (i < MAP_N) { s_map[i].h = *ret; s_map[i].addr = (uint8_t)cfg->device_address; }   /* (full: its failures count under 0x00) */
    }
    i2c_guard_unlock(); return e;
}
esp_err_t __wrap_i2c_master_bus_rm_device(i2c_master_dev_handle_t d) {
    i2c_guard_lock();
    esp_err_t e = __real_i2c_master_bus_rm_device(d);
    if (e == ESP_OK) for (int i = 0; i < MAP_N; i++) if (s_map[i].h == d) { s_map[i].h = NULL; s_map[i].addr = 0; }
    i2c_guard_unlock(); return e;
}

void i2c_guard_boot_clear(int sda, int scl) {
    if (s_rtc_check == ~s_rtc_word && (s_rtc_word & 0xFF) == RTC_TAG) {   /* the run before's breadcrumb (a reset that kept RTC memory) */
        s_prev_inflight = (uint8_t)(s_rtc_word >> 8); s_prev_fails = (uint16_t)(s_rtc_word >> 16);
    }
    rtc_put(0);
    if (!s_mx) s_mx = xSemaphoreCreateRecursiveMutexStatic(&s_mx_buf);
    s_sda = sda; s_scl = scl;
    gpio_config_t io = { .pin_bit_mask = (1ULL << sda) | (1ULL << scl), .mode = GPIO_MODE_INPUT_OUTPUT_OD, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    gpio_set_level(scl, 1); gpio_set_level(sda, 1);
    esp_rom_delay_us(10);
    const bool sda_low = !gpio_get_level(sda);
    bool scl_low = !gpio_get_level(scl);
    for (int t = 0; scl_low && t < 100; t++) { esp_rom_delay_us(10); scl_low = !gpio_get_level(scl); }   /* a slave stretching the clock: up to 1 ms */
    /* as ESP-IDF's software clear: SCL low, SDA released; pulses while SDA stays low (at most nine - a slave sending
       a byte lets go by its ACK bit at the latest), then the STOP: SDA low -> high with SCL high */
    gpio_set_level(scl, 0); esp_rom_delay_us(5);
    for (int i = 0; i < 9 && !gpio_get_level(sda); i++) {
        gpio_set_level(scl, 1); esp_rom_delay_us(5);
        gpio_set_level(scl, 0); esp_rom_delay_us(5);
    }
    gpio_set_level(sda, 0); esp_rom_delay_us(5);
    gpio_set_level(scl, 1); esp_rom_delay_us(5);
    gpio_set_level(sda, 1); esp_rom_delay_us(10);
    s_boot_stuck = sda_low || scl_low;
    s_boot_freed = s_boot_stuck && gpio_get_level(sda) && gpio_get_level(scl);
    if (s_boot_stuck)
        ESP_LOGW(TAG, "the bus was STUCK at boot (SDA %s, SCL %s): clocked and stopped - %s", sda_low ? "LOW" : "high", scl_low ? "LOW" : "high",
                 s_boot_freed ? "both lines free now" : "STILL HELD (a chip holds a line: only a power cut frees it)");
}
bool     i2c_guard_boot_stuck(void)    { return s_boot_stuck; }
bool     i2c_guard_boot_freed(void)    { return s_boot_freed; }
uint8_t  i2c_guard_prev_inflight(void) { return s_prev_inflight; }
uint16_t i2c_guard_prev_fails(void)    { return s_prev_fails; }
uint32_t i2c_guard_fails(void)         { return s_fails; }
uint32_t i2c_guard_recoveries(void)    { return s_recov; }
uint8_t  i2c_guard_last_addr(void)     { return s_last; }

static const char *chip(uint8_t a) {
    switch (a) {
    case 0x34: return "PMIC AXP2101"; case 0x38: return "touch FT3168/FT5x06"; case 0x15: return "touch CST816"; case 0x5A: return "touch CST92xx";
    case 0x51: return "RTC PCF85063"; case 0x6A: case 0x6B: return "IMU QMI8658"; case 0x18: return "codec ES8311"; case 0x20: return "IO expander";
    case 0x40: case 0x41: case 0x42: case 0x43: return "mic ADC ES7210"; case 0: return "(a device not in the map)"; default: return "?";
    }
}
void i2c_guard_report(void) {
    ESP_LOGI(TAG, "I2C this boot: %lu failed transactions, %lu bus recoveries%s%s", (unsigned long)s_fails, (unsigned long)s_recov,
             s_boot_stuck ? (s_boot_freed ? " | the bus was STUCK at boot and freed" : " | the bus was STUCK at boot and NOT freed") : "",
             s_prev_inflight ? " | the run before stopped INSIDE a transaction" : "");
    if (s_prev_inflight) ESP_LOGW(TAG, "  the run before stopped inside a transaction to 0x%02x (%s), after %u failures", s_prev_inflight, chip(s_prev_inflight), s_prev_fails);
    for (int k = 0; k < ST_N; k++)
        if (s_st[k].fail || s_st[k].tmo) ESP_LOGI(TAG, "  0x%02x %-22s %u failed, %u of them timeouts", s_st[k].addr, chip(s_st[k].addr), s_st[k].fail + s_st[k].tmo, s_st[k].tmo);
}
