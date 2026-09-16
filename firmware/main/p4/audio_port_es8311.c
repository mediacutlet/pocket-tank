/* audio_port_es8311.c - I2S -> ES8311 -> the on-board amp (GPIO 53) ->
 * the speaker, for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B.
 * See audio_port.h. Pins from the 4B schematic: SCLK=12 MCLK=13 LCLK=10
 * DOUT=9 DSIN=11 AMP_EN=53 (the codec talks ES8311 over the BSP I2C bus,
 * owned by codec_port - initialised before us from board_i2c_bus()). */
#include "audio_port.h"
#include "audio.h"
#include "codec_port.h"
#include "battery_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "audio";

#define PIN_I2S_MCLK  13
#define PIN_I2S_BCLK  12
#define PIN_I2S_WS    10
#define PIN_I2S_DOUT  9        /* ESP -> codec DSDIN */
#define PIN_AMP_EN    53       /* the 4B's speaker amp enable */
#define BLOCK         160      /* 10 ms at 16 kHz */
#define IDLE_US       (2 * 1000000)
#define CODEC_RAIL    "aldo1"  /* the S3's A3V3 codec rail: a no-op on the 4B (no PMIC stub) */

extern const uint8_t _binary_sounds_bin_start[];
extern const uint8_t _binary_sounds_bin_end[];

static i2s_chan_handle_t  s_tx;
static SemaphoreHandle_t  s_mx;
static TaskHandle_t       s_task;
static bool s_ok, s_up;
static volatile bool s_sleep_req, s_warm_req;
static int64_t s_quiet_since;
static int s_volume = 2;
static int16_t s_buf[BLOCK];
/* cold-start settle, tunable live (director `snd settle <codec ms> <amp ms>`,
   `snd idle <s>`) until the bench says what the DAC and the amp need */
static int s_settle_codec_ms = 150, s_settle_amp_ms = 100;
/* the codec + amp need > 250 ms from cold and a tap's dwell cannot hide
   that (a cold card cue was lost), so the port stays warm while the tank is
   HANDLED: any touch (main.c -> audio_port_prewarm) brings it up and
   restarts this idle clock, and it goes down idle_us after the last touch
   or sound - on a table, silent. 0 = warm while awake; `snd idle N` sets it
   live. */
static int64_t s_idle_us = 5 * 1000000LL;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void amp(bool on) { gpio_set_level(PIN_AMP_EN, on); }

/* Deep sleep: the I2S lines and the amp's enable are the ESP's outputs into
 * ICs that stay powered. In deep sleep an un-held digital pad is neither
 * driven nor pulled - and esp-idf isolates the rest of the digital pads only
 * once digital hold is on. So before esp_deep_sleep_start: each of these a
 * plain GPIO, driven low, held. The codec is already down (audio_port_sleep)
 * and the wake is a reboot; audio_port_init releases the holds before the
 * drivers claim the pins again. */
static const gpio_num_t QUIET_PINS[] = { PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_AMP_EN };
void audio_port_deep_sleep_pins(void) {
    for (size_t i = 0; i < sizeof QUIET_PINS / sizeof QUIET_PINS[0]; i++) {
        gpio_num_t p = QUIET_PINS[i];
        gpio_reset_pin(p);                        /* off the I2S matrix routing, a GPIO again */
        gpio_set_direction(p, GPIO_MODE_OUTPUT);
        gpio_set_level(p, 0);
        gpio_hold_en(p);
    }
}
static void release_pins(void) {
    for (size_t i = 0; i < sizeof QUIET_PINS / sizeof QUIET_PINS[0]; i++) gpio_hold_dis(QUIET_PINS[i]);
}

static void write_silence(int ms) {
    memset(s_buf, 0, sizeof s_buf);
    size_t w;
    for (int i = 0; i < ms / 10; i++) i2s_channel_write(s_tx, s_buf, sizeof s_buf, &w, 100);
}

/* power up: clocks -> codec -> zeros -> amp (pops stay inside) */
static void bring_up(void) {
    int64_t t0 = esp_timer_get_time();
    battery_port_set_rail(CODEC_RAIL, true);      /* no-op on the 4B (USB-C powered, no PMIC) */
    vTaskDelay(pdMS_TO_TICKS(5));
    i2s_channel_enable(s_tx);                 /* MCLK/BCLK/LRCK running before the CSM starts */
    bool ok = codec_port_up();
    write_silence(s_settle_codec_ms);         /* the DAC's vmid / reference settle (fast charge, then normal) */
    codec_port_settled();
    amp(true);
    write_silence(s_settle_amp_ms);           /* the amp's own start-up (pop suppression) */
    s_up = true; s_quiet_since = 0;
    ESP_LOGI(TAG, "up in %lld ms%s", (esp_timer_get_time() - t0) / 1000, ok ? "" : " (codec writes FAILED)");
}
/* power down: amp -> zeros -> clocks off -> codec down */
static void bring_down(void) {
    amp(false);
    write_silence(10);
    i2s_channel_disable(s_tx);
    codec_port_down();
    battery_port_set_rail(CODEC_RAIL, false);   /* no-op on the 4B */
    s_up = false;
    ESP_LOGI(TAG, "down (idle)");
}

static void player(void *arg) {
    (void)arg;
    for (;;) {
        if (!s_up) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            if (s_sleep_req) { s_sleep_req = false; continue; }
            xSemaphoreTake(s_mx, portMAX_DELAY);
            bool any = audio_active() || s_warm_req;
            xSemaphoreGive(s_mx);
            s_warm_req = false;
            if (!any) continue;
            bring_up();
        }
        xSemaphoreTake(s_mx, portMAX_DELAY);
        int live = audio_render(s_buf, BLOCK);
        xSemaphoreGive(s_mx);
        size_t w;
        i2s_channel_write(s_tx, s_buf, sizeof s_buf, &w, 100);   /* blocks ~10 ms: the DMA paces us */
        int64_t now = esp_timer_get_time();
        if (live) s_quiet_since = 0;
        else if (!s_quiet_since) s_quiet_since = now;
        if (s_sleep_req || (s_idle_us && s_quiet_since && now - s_quiet_since > s_idle_us)) {
            if (s_sleep_req) { xSemaphoreTake(s_mx, portMAX_DELAY); audio_stop_all(); xSemaphoreGive(s_mx); }
            bring_down();
            s_sleep_req = false;
        }
    }
}

static void load_volume(void) {
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u8(h, "snd", &v) == ESP_OK && v <= 2) s_volume = v;
    nvs_close(h);
}

bool audio_port_init(i2c_master_bus_handle_t bus) {
    (void)bus;                                 /* the codec's I2C device lives in codec_port (initialised before us) */
    release_pins();                            /* a deep-sleep wake is a boot: drop the holds first */
    size_t bank_bytes = (size_t)(_binary_sounds_bin_end - _binary_sounds_bin_start);
    if (bank_bytes != SND_BANK_BYTES) { ESP_LOGW(TAG, "bank is %u bytes, sounds.h says %u: rebuild (tools/make_sounds.py build) - silent", (unsigned)bank_bytes, (unsigned)SND_BANK_BYTES); return false; }
    if (!codec_port_present()) { ESP_LOGW(TAG, "no ES8311: silent"); return false; }
    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_AMP_EN, .mode = GPIO_MODE_OUTPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE };
    gpio_config(&io); amp(false);
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.dma_desc_num = 4; cc.dma_frame_num = BLOCK;
    if (i2s_new_channel(&cc, &s_tx, NULL) != ESP_OK) { ESP_LOGE(TAG, "no I2S channel"); return false; }
    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SND_RATE),              /* MCLK = 256 fs = 4.096 MHz */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS, .dout = PIN_I2S_DOUT, .din = I2S_GPIO_UNUSED,
                      .invert_flags = { 0 } },
    };
    if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) { ESP_LOGE(TAG, "I2S std init failed"); return false; }
    audio_init((const int16_t *)_binary_sounds_bin_start, SND_BANK_SAMPLES);   /* flash-mapped: no RAM */
    load_volume(); audio_set_volume(s_volume);
    s_mx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(player, "audio", 4096, NULL, 6, &s_task, 1);
    s_ok = true;
    int present = 0; for (int i = 0; i < SND_COUNT; i++) present += SND_CUES[i].n_var > 0;
    ESP_LOGI(TAG, "%d of %d cues, bank %u KB in flash, volume %d; warm while handled (touch), down %lld s after", present, SND_COUNT, (unsigned)(SND_BANK_BYTES / 1024), s_volume, s_idle_us / 1000000);
    return true;
}

void audio_port_play(int cue, int pitch_q8) {
    if (!s_ok) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool started = audio_play(cue, pitch_q8, now_ms());
    xSemaphoreGive(s_mx);
    if (started) xTaskNotifyGive(s_task);
}
void audio_port_prewarm(void) {
    if (!s_ok) return;
    s_quiet_since = 0;                          /* activity: the idle clock restarts */
    if (s_up) return;
    s_warm_req = true; xTaskNotifyGive(s_task);
}
void audio_port_stop(int cue) {
    if (!s_ok) return;
    xSemaphoreTake(s_mx, portMAX_DELAY); audio_stop(cue); xSemaphoreGive(s_mx);
}
void audio_port_set_volume(int level) {
    s_volume = level < 0 ? 0 : level > 2 ? 2 : level;
    if (s_ok) { xSemaphoreTake(s_mx, portMAX_DELAY); audio_set_volume(s_volume); xSemaphoreGive(s_mx); }
    nvs_handle_t h;
    if (nvs_open("tank", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "snd", (uint8_t)s_volume); nvs_commit(h); nvs_close(h); }
    ESP_LOGI(TAG, "volume %s", s_volume == 0 ? "off" : s_volume == 1 ? "quiet" : "normal");
}
int  audio_port_volume(void) { return s_volume; }
void audio_port_set_night(bool night) {
    if (!s_ok) return;
    xSemaphoreTake(s_mx, portMAX_DELAY); audio_set_night(night); xSemaphoreGive(s_mx);
}
void audio_port_sleep(void) {
    if (!s_ok || !s_up) return;
    s_sleep_req = true; xTaskNotifyGive(s_task);
    for (int i = 0; i < 50 && s_up; i++) vTaskDelay(pdMS_TO_TICKS(10));   /* one block + the down sequence */
    if (s_up) ESP_LOGW(TAG, "still up at sleep: forcing the amp low");
    amp(false);
}
void audio_port_tune(int codec_ms, int amp_ms, int idle_s) {
    if (codec_ms >= 0) s_settle_codec_ms = codec_ms;
    if (amp_ms >= 0) s_settle_amp_ms = amp_ms;
    if (idle_s >= 0) s_idle_us = (int64_t)idle_s * 1000000;
    ESP_LOGI(TAG, "settle codec %d ms + amp %d ms, idle %lld s (0 = warm while awake)", s_settle_codec_ms, s_settle_amp_ms, s_idle_us / 1000000);
}
bool audio_port_up(void) { return s_up; }
const char *audio_port_state(void) { return !s_ok ? "absent" : s_up ? "up" : "idle"; }
