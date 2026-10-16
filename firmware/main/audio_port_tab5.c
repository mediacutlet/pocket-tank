/* audio_port_tab5.c - the Tab5's sound: I2S -> ES8388 -> the speaker amp on the
 * IO expander. See audio_port.h and docs/AUDIO.md for the design (the mixer in
 * common/audio.c, 10 ms blocks off the tank task, prewarm under the finger).
 *
 * THIS PORT IS WRITTEN, AND ITS I2S IS STILL SHORT OF WORKING. What is known, all
 * of it measured on this board (ESP32-P4 silicon rev v1.3) with a 60-line
 * standalone probe, on ESP-IDF v5.5.6 and v5.4.2 alike:
 *
 *   1. `i2s_channel_preload_data()` **must** be called while the channel is
 *      disabled, before enable. Without it the DMA's descriptor queue is empty
 *      from the start and every `i2s_channel_write` waits for a free descriptor
 *      that never comes: ESP_ERR_TIMEOUT with 0 bytes. With it, writes move data.
 *      It only accepts what fits the DMA buffer (3072 bytes at the driver's
 *      default 6 x 512 geometry; a 3200-byte preload loads 3072, and a write
 *      larger than the buffer times out).
 *   2. After the preload, a **direct `i2s_channel_write` works and the codec
 *      dev's own I2S data interface does not** (measured back to back in this
 *      port: direct 512/512 bytes OK, `esp_codec_dev_write` timed out). So the
 *      codec dev keeps the ES8388 registers, volume and mute, and the samples go
 *      to the channel directly (write_block below).
 *   3. What still stops it: after exactly two accepted writes (2 KB of the 3 KB
 *      buffer) the DMA goes quiet and every later write times out, while
 *      `bck_cnt.tx_bck_cnt` freezes at 17 - i.e. the peripheral stops, not the
 *      descriptor chain. That is reproducible in every shape tried: TX-only and
 *      duplex, an RX in TDM (as M5's own BSP does), 16 and 48 kHz, MONO and
 *      STEREO, MCLK on GPIO 30 or unused, `bclk_div` 0 or 8, `auto_clear` on or
 *      off, `I2S_CLK_SRC_DEFAULT`/`XTAL`/`APLL`, `dma_frame_num` matched to the
 *      block or left at the default. M5Stack's own Tab5 demo (ESP-IDF v5.4.2)
 *      plays audio on this hardware, so its configuration differs somewhere this
 *      probe has not found - the next step is to build that demo and bisect.
 *   4. Corrected on the way, and worth not repeating: the "dead BCLK" reading
 *      from sampling the pad was an artefact (the pad's input path was never
 *      enabled); the peripheral's own counter is the honest instrument. And
 *      `I2S_CLK_SRC_DEFAULT` on pre-v3 silicon maps to XTAL in IDF 5.5's
 *      i2s_ll.h, so the port's earlier explicit `I2S_CLK_SRC_APLL` was an
 *      unnecessary guess and is gone.
 *
 * What it does about all that: bring_up() bounds itself. If a silence write
 * fails, the port gives up on that bring-up, says so once (with the numbers), and
 * answers every later cue without touching the I2S again - the tank runs, silent,
 * instead of losing a second per 10 ms block to write timeouts. `snd` on the
 * console prints "i2s-dead", and docs/tab5-next.md item 2 carries the full trail
 * for whoever picks this up. */
#include "audio_port.h"
#include "audio.h"
#include "board_pins.h"
#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "audio";

#define BLOCK         160      /* 10 ms at 16 kHz */
#define IDLE_US       (2 * 1000000LL)

extern const uint8_t _binary_sounds_bin_start[];
extern const uint8_t _binary_sounds_bin_end[];

static i2s_chan_handle_t  s_tx;
static esp_codec_dev_handle_t s_spk;
static SemaphoreHandle_t  s_mx;
static TaskHandle_t       s_task;
static bool s_ok, s_up, s_i2s_dead;
static volatile bool s_sleep_req, s_warm_req;
static int64_t s_quiet_since;
static int s_volume = 2;
static int16_t s_buf[BLOCK];
/* what the current bring-up has handed the DMA (the log line at the end of a cue
 * is the proof the stream ran; see docs/tab5-next.md item 2) */
static unsigned s_played_blocks, s_played_bytes;
/* the ES8388's DAC reference and the amp's own start-up: the same tunables as
 * the ES8311 port (`snd settle <codec ms> <amp ms>` off the director) */
static int s_settle_codec_ms = 150, s_settle_amp_ms = 100;
static int64_t s_idle_us = 5 * 1000000LL;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* the speaker amp is expander #0's bit 1 (BSP_SPEAKER_EN): the BSP's own
 * bsp_feature_enable is what its speaker init used, so use it here too */
static void amp(bool on) {
    if (bsp_feature_enable(BSP_FEATURE_SPEAKER, on) != ESP_OK)
        ESP_LOGW(TAG, "speaker enable %s failed", on ? "on" : "off");
}

/* THE WRITE PATH (2026-10-17). The tank's sound goes straight to the I2S
 * channel, not through `esp_codec_dev_write`. Measured on this unit: after the
 * preload, a direct `i2s_channel_write` moves 512/512 bytes, while the codec
 * dev's own I2S data interface times out with 0 bytes on every call - so the
 * codec dev stays for what it is good at (ES8388 registers, volume, mute, the
 * sample-rate setup) and the samples go to the peripheral directly.
 * false = the I2S did not take the block. */
static bool write_block(const void *buf, size_t n) {
    size_t w = 0;
    return i2s_channel_write(s_tx, buf, n, &w, pdMS_TO_TICKS(500)) == ESP_OK && w == n;
}
/* one block of silence; false = the I2S did not take it (see the header) */
static bool write_silence(int ms) {
    memset(s_buf, 0, sizeof s_buf);
    for (int i = 0; i < ms / 10; i++)
        if (!write_block(s_buf, sizeof s_buf)) return false;
    return true;
}
/* after the first failure there is no point trying again: say so once, with the
 * numbers, and let the tank be silent and quick rather than silent and slow */
static void i2s_dead(const char *where) {
    if (s_i2s_dead) return;
    s_i2s_dead = true;
    ESP_LOGW(TAG, "I2S takes no data (%s): a write timed out with 0 bytes (the preload note in "
                  "bring_up has the story) - the tank stays silent", where);
}

/* power up: codec open (which enables the I2S) -> THE PRELOAD -> zeros -> amp -> zeros */
static void bring_up(void) {
    int64_t t0 = esp_timer_get_time();
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = SND_RATE };
    bool ok = esp_codec_dev_open(s_spk, &fs) == 0;
    if (!ok) { i2s_dead("codec open"); esp_codec_dev_close(s_spk); return; }
    /* THE TRAP THIS PORT FELL INTO (2026-10-17), and the reason the tank was
     * silent: on this chip the DMA's descriptor queue is only filled by
     * `i2s_channel_preload_data()`, and that call is refused while the channel is
     * enabled (ESP_ERR_INVALID_STATE). Without it every `i2s_channel_write` waits
     * for a free descriptor that never comes and times out with 0 bytes - while
     * the bit clock runs perfectly the whole time (`bck_cnt.tx_bck_cnt` advances;
     * the "dead BCLK" readings from the pin were the pad's input path, not a
     * stopped clock). M5Stack's own demo never meets this; its codec path leaves
     * descriptors preloaded.
     * So, with the channel the codec's open just enabled: disable -> preload
     * silence -> enable. Established with a 60-line probe on this unit, IDF
     * 5.4.2 and 5.5.6 alike (docs/tab5-next.md item 2). */
    /* preload FIRST (the channel is normally still disabled here); if the
     * codec's open did enable it, IDF answers ESP_ERR_INVALID_STATE and only
     * then do the disable/preload dance. Calling disable on a channel that was
     * never enabled trips the driver's own bookkeeping ("the channel has not
     * been enabled yet") and is what kept this from working. */
    memset(s_buf, 0, sizeof s_buf);
    size_t loaded = 0;
    esp_err_t pr = i2s_channel_preload_data(s_tx, s_buf, sizeof s_buf, &loaded);
    if (pr == ESP_ERR_INVALID_STATE) {
        i2s_channel_disable(s_tx);
        pr = i2s_channel_preload_data(s_tx, s_buf, sizeof s_buf, &loaded);
    }
    esp_err_t en = i2s_channel_enable(s_tx);
    if (pr != ESP_OK || en != ESP_OK) {
        ESP_LOGW(TAG, "preload %s (%u bytes) / enable %s", esp_err_to_name(pr), (unsigned)loaded, esp_err_to_name(en));
        i2s_dead("preload"); esp_codec_dev_close(s_spk); return;
    }
    /* the direct-write check that found the codec dev's data path broken, and the
     * step-by-step trace with the peripheral's BCLK counter, live in the commit
     * and in docs/tab5-next.md item 2 - not in the hot path */
    if (!write_silence(s_settle_codec_ms)) { i2s_dead("bring-up"); esp_codec_dev_close(s_spk); return; }
    amp(true);
    if (!write_silence(s_settle_amp_ms)) { i2s_dead("after the amp came up"); amp(false); esp_codec_dev_close(s_spk); return; }
    s_up = true; s_quiet_since = 0;
    s_played_blocks = 0; s_played_bytes = 0;
    s_warm_req = false;   /* a request that stood while this came up is answered */
    ESP_LOGI(TAG, "up in %lld ms (preloaded %u bytes)", (esp_timer_get_time() - t0) / 1000, (unsigned)loaded);
}
/* power down: amp -> zeros -> codec closed (which disables the I2S) */
static void bring_down(void) {
    write_silence(10);
    amp(false);
    esp_codec_dev_close(s_spk);
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
            if (!s_up) continue;              /* the I2S said no: nothing to play into */
        }
        xSemaphoreTake(s_mx, portMAX_DELAY);
        int live = audio_render(s_buf, BLOCK);
        xSemaphoreGive(s_mx);
        if (!write_block(s_buf, sizeof s_buf)) { i2s_dead("playing"); bring_down(); continue; }
        /* how much the DAC actually received on this bring-up: the honest measure
         * that the stream is flowing (the ear is the last link, not the first) */
        s_played_blocks++; s_played_bytes += sizeof s_buf;
        int64_t now = esp_timer_get_time();
        if (live) s_quiet_since = 0;
        else if (!s_quiet_since) s_quiet_since = now;
        if (s_sleep_req || (s_idle_us && s_quiet_since && now - s_quiet_since > s_idle_us)) {
            bool sleeping = s_sleep_req;
            if (sleeping) { xSemaphoreTake(s_mx, portMAX_DELAY); audio_stop_all(); xSemaphoreGive(s_mx); }
            ESP_LOGI(TAG, "played %u blocks (%u bytes, %u ms of audio) before going quiet%s",
                     (unsigned)s_played_blocks, (unsigned)s_played_bytes,
                     (unsigned)(s_played_bytes / 2 / (SND_RATE / 1000)), sleeping ? " on request" : "");
            s_played_blocks = 0; s_played_bytes = 0;
            bring_down();
            s_sleep_req = false;
            if (sleeping) { s_warm_req = false; ulTaskNotifyTake(pdTRUE, 0); }
        }
    }
}

static void load_volume(void) {
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u8(h, "snd", &v) == ESP_OK && v <= 2) s_volume = v;
    nvs_close(h);
}

/* the tank's three levels -> the codec's 0..100 (the ES8311 port leaves its own
 * DAC at 0 dB and mixes in software; here the chip's own gain tracks ours) */
static int codec_gain(int level) { return level <= 0 ? 0 : level == 1 ? 45 : 75; }

bool audio_port_init(i2c_master_bus_handle_t bus) {
    size_t bank_bytes = (size_t)(_binary_sounds_bin_end - _binary_sounds_bin_start);
    if (bank_bytes != SND_BANK_BYTES) { ESP_LOGW(TAG, "bank is %u bytes, sounds.h says %u: rebuild (tools/make_sounds.py build) - silent", (unsigned)bank_bytes, (unsigned)SND_BANK_BYTES); return false; }
    /* the channel is this port's own, not the BSP's: bsp_audio_codec_speaker_init()
     * hides its channel inside the codec dev, and this way the idle state really
     * does stop the clocks (docs/AUDIO.md's power story) */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    /* auto_clear: without it an underrun stops the TX FIFO asking the DMA for
     * more data, so the descriptor queue never refills and every write after the
     * first one times out (the BSP and M5's demo both set it). */
    cc.auto_clear = true;
    /* the DMA geometry stays at the driver's default: a custom dma_desc_num /
     * dma_frame_num is one of the two things that kept the preload fix from
     * working, and IDF re-aligns the frame count to 64 bytes anyway. */
    if (i2s_new_channel(&cc, &s_tx, NULL) != ESP_OK) { ESP_LOGE(TAG, "no I2S channel"); return false; }
    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SND_RATE),               /* MCLK = 256 fs */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = T_PIN_I2S_MCLK, .bclk = T_PIN_I2S_BCLK, .ws = T_PIN_I2S_WS,
                      .dout = T_PIN_I2S_DOUT, .din = I2S_GPIO_UNUSED,
                      .invert_flags = { 0 } },
    };
    /* I2S_CLK_SRC_DEFAULT is what the working probe used on this unit: IDF 5.5
     * maps it to XTAL for pre-v3 silicon (i2s_ll.h's CHIP_SUPPORT_MIN_REV check),
     * and asking for the APLL by name was the port's own earlier guess - it is
     * the second thing that kept the preload fix from working. */
    std.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK) { ESP_LOGE(TAG, "I2S std init failed"); return false; }
    /* the codec chain, exactly the BSP's parameters (audio_codec_new_* own what
     * they are given, so these are static) */
    static audio_codec_i2s_cfg_t i2s_cfg;
    i2s_cfg = (audio_codec_i2s_cfg_t){ .port = I2S_NUM_1, .tx_handle = s_tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) { ESP_LOGE(TAG, "no I2S data interface"); return false; }
    audio_codec_i2c_cfg_t i2c_cfg = { .port = 0, .addr = ES8388_CODEC_DEFAULT_ADDR, .bus_handle = bus };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    if (!ctrl || !gpio) { ESP_LOGE(TAG, "no I2C control / GPIO interface"); return false; }
    const esp_codec_dev_hw_gain_t gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 };
    es8388_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl, .gpio_if = gpio, .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC, .pa_reverted = false, .master_mode = false, .hw_gain = gain,
    };
    const audio_codec_if_t *codec_if = es8388_codec_new(&codec_cfg);
    if (!codec_if) { ESP_LOGW(TAG, "no ES8388 at 0x%02x: silent", ES8388_CODEC_DEFAULT_ADDR); return false; }
    const esp_codec_dev_cfg_t dev_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec_if, .data_if = data_if };
    s_spk = esp_codec_dev_new(&dev_cfg);
    if (!s_spk) { ESP_LOGE(TAG, "no codec device"); return false; }
    amp(false);
    audio_init((const int16_t *)_binary_sounds_bin_start, SND_BANK_SAMPLES);   /* flash-mapped: no RAM */
    load_volume(); audio_set_volume(s_volume);
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = SND_RATE };
    if (esp_codec_dev_open(s_spk, &fs) != 0) { ESP_LOGE(TAG, "codec open failed"); return false; }
    esp_codec_dev_set_out_vol(s_spk, codec_gain(s_volume));
    /* open() enabled the channel and opened the codec: go straight back to the
     * idle state (amp still low, codec closed) - the tank is silent until it has
     * something to say */
    esp_codec_dev_close(s_spk);
    s_mx = xSemaphoreCreateMutex();
    if (xTaskCreatePinnedToCore(player, "audio", 4096, NULL, 6, &s_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "no player task"); return false;
    }
    s_ok = true;
    int present = 0; for (int i = 0; i < SND_COUNT; i++) present += SND_CUES[i].n_var > 0;
    ESP_LOGI(TAG, "ES8388 ready at 0x%02x: %d of %d cues, bank %u KB in flash, volume %d; warm while handled (touch / IMU), down %lld s after",
             ES8388_CODEC_DEFAULT_ADDR, present, SND_COUNT, (unsigned)(SND_BANK_BYTES / 1024), s_volume, s_idle_us / 1000000);
    return true;
}

void audio_port_play(int cue, int pitch_q8) {
    if (!s_ok || s_i2s_dead) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool started = audio_play(cue, pitch_q8, now_ms());
    xSemaphoreGive(s_mx);
    if (started) xTaskNotifyGive(s_task);
}
void audio_port_prewarm(void) {
    if (!s_ok || s_i2s_dead) return;
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
    if (s_ok) {
        xSemaphoreTake(s_mx, portMAX_DELAY); audio_set_volume(s_volume); xSemaphoreGive(s_mx);
        esp_codec_dev_set_out_vol(s_spk, codec_gain(s_volume));
    }
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
    if (s_up) { ESP_LOGW(TAG, "still up at sleep: forcing the amp low"); amp(false); }
}
void audio_port_deep_sleep_pins(void) {
    /* Nothing to hold here: the tab5's deep sleep is not brought up (no PWR
     * sense line; main.c's P4 path skips the ext0 wake) and the I2S pins and the
     * amp belong to this port's channel, which is closed before any sleep. */
}
void audio_port_tune(int codec_ms, int amp_ms, int idle_s) {
    if (codec_ms >= 0) s_settle_codec_ms = codec_ms;
    if (amp_ms >= 0) s_settle_amp_ms = amp_ms;
    if (idle_s >= 0) s_idle_us = (int64_t)idle_s * 1000000;
    ESP_LOGI(TAG, "settle codec %d ms + amp %d ms, idle %lld s (0 = warm while awake)", s_settle_codec_ms, s_settle_amp_ms, s_idle_us / 1000000);
}
bool audio_port_up(void) { return s_up; }
const char *audio_port_state(void) { return !s_ok ? "absent" : s_i2s_dead ? "i2s-dead" : s_up ? "up" : "idle"; }
