/* main.c — pocket-tank firmware entry (ESP32-S3).
 *   core 0: tank reflex layer + render at 60 fps, frames to the display port
 *   core 1: LLM advisor (q4_model over the mmap'd flash model partition)
 * Boot: assert the PSRAM plan, mmap the model partition, start both loops.
 * Without a panel (QEMU / bring-up) the display port is a counting stub and
 * every decision + tok/s goes to the log. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_memory_utils.h"
#include "tank.h"
#include "advisor.h"
#include "render.h"
#include "psram_plan.h"
#include "display_port.h"
#include "advisor_llm_esp.h"
#include "touch_port.h"
#include "battery_port.h"
#include "battery.h"
#include "imu_port.h"
#include "director.h"
#include "brightness.h"
#include "orientation.h"
#include "batlog.h"
#include "codec_port.h"
#include "progression.h"
#include "audio_port.h"
#include "audio.h"
#include "notice.h"
#include "tank_events.h"
#include "setup.h"
#include "nvs_flash.h"
#include "esp_app_desc.h"
#include "version.h"
#include "rtc_port.h"
#include "driver/i2c_master.h"
#include "esp_async_memcpy.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

/* BOOT (GPIO0, active low, RTC-wake capable): the reset chord (held + a tap
 * on the glass), the director's deep-sleep wake, and the sleep key only on a
 * board with no PMIC. Since 2026-09-16 the sleep key is the PWR key (the
 * AXP2101's PWRON, polled over I2C): the night mode is a PMIC power-off,
 * from which ONLY that key (or USB) can bring the board back, so it has to
 * be the one key the keeper ever presses - press to sleep, press to wake. */
#define BTN_SLEEP GPIO_NUM_0
static bool s_pmic;                        /* an AXP2101 answered: the PWR key exists, power-off is real */
static bool s_imu;                         /* an IMU answered: it turns the picture (and, on the CYD, the face-down gesture) */
static bool s_sleep_by_face;               /* the next sleep was the face-down gesture: face up wakes it */
#if defined(CONFIG_POCKET_TANK_DISPLAY_SH8601) || defined(CONFIG_POCKET_TANK_DISPLAY_ILI9341)
extern i2c_master_bus_handle_t board_i2c_bus(void);
#else
static i2c_master_bus_handle_t board_i2c_bus(void) { return NULL; }
#endif

/* tokenizer.bin is tiny: embed it in the app image */
extern const uint8_t tokenizer_bin_start[] asm("_binary_tokenizer_bin_start");
extern const uint8_t tokenizer_bin_end[]   asm("_binary_tokenizer_bin_end");

static const char *TAG = "pocket-tank";
static tank_t tank;
static uint16_t *fb[PLAN_FB_COUNT];
static bool llm_ok = false;

/* GDMA prefetch of the static scene into the idle framebuffer: overlaps the
 * 320 KB scene restore with tank logic + the frame sleep instead of a CPU
 * memcpy inside render_tank. */
static async_memcpy_handle_t s_amc;
static SemaphoreHandle_t s_amc_done;
static bool s_prefetch_pending; static uint16_t *s_prefetch_fb; static unsigned s_prefetch_ep;

static bool amc_cb(async_memcpy_handle_t h, async_memcpy_event_t *ev, void *ctx) {
    (void)h; (void)ev; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_amc_done, &hp);
    return hp == pdTRUE;
}

static void assert_plan(void) {
    size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_ps = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_in = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "PSRAM total %u KB free %u KB | internal free %u KB",
             (unsigned)psram / 1024, (unsigned)free_ps / 1024, (unsigned)free_in / 1024);
    size_t need = PLAN_FB_TOTAL + PLAN_KV_BYTES + PLAN_ACT_BYTES + PLAN_PSRAM_MIN_FREE_AFTER;
    if (free_ps < need)
        ESP_LOGW(TAG, "PSRAM plan NOT met (need %u KB, have %u KB) - degraded mode (QEMU?)",
                 (unsigned)need / 1024, (unsigned)free_ps / 1024);
    else
        ESP_LOGI(TAG, "PSRAM plan OK (need %u KB)", (unsigned)need / 1024);
}

static void enter_poweroff(void);
static void deep_sleep_now(int wake_after_s);
static bat_t s_bh;                          /* the battery page's history: NVS "bat"/"hist" (its own namespace - a tank reset leaves it) */
static void bat_hist_save(void);            /* at sleep too: the screen-on time so far */
static bool s_btn_armed; static int64_t s_btn_low_since;   /* sleep_button_poll state */
static bool s_btn_used;   /* this press opened the reset prompt: no drowse, no power-off from it */

/* Sleep (2026-09-14, revised the same day after Strato found a quick
 * sleep/wake "feels like a soft boot"): two stages.
 *  1. GRACE, 20 min (was 90 s until 2026-09-16 evening): save, panel and touch off, IMU quiesced, then RAM-alive
 *     LIGHT sleep with BOOT (GPIO, level low) and a timer armed. A press in
 *     this window resumes IN PLACE - the fish exactly where they were,
 *     mid-goal - after crediting the nap to tank_tick_sleep. Costs what the
 *     old drowse did (~4.7 mA), for 20 min at most - ~1.6 mAh per sleep.
 *     Why 20 min and not 90 s: from the PMIC power-off only a PWRON hold
 *     longer than ONLEVEL (128 ms, the shortest the AXP2101 offers) boots
 *     the board; a lighter tap does nothing at all (Strato's presses time
 *     at ~150 ms on the PMIC's own clock - director `keytime`). Inside the
 *     grace the chip is awake and any tap wakes it, so the short absences
 *     stay a tap and only a real absence ends in the power-off.
 *  2. DEEP sleep once the grace passes: BOOT armed as ext0, chip down to
 *     microamps, RAM and PSRAM gone. Waking is a boot: app_main sees the
 *     ext0 (or the director's timer) wake cause and calls progression_wake -
 *     restore the save, then ONE tank_tick_sleep for the real time since it
 *     was written (the grace included; nothing ticked during it) - and then
 *     puts every fish back where it fell asleep, on the goal it had, from a
 *     snapshot kept in RTC slow memory (survives deep sleep, not power-off:
 *     after a cold boot the fish may scatter, and that is fine).
 *  Since 2026-09-16 (the night the cell died; Strato: one key, no modes to
 *  remember, "an extended sleep is exactly that, night or noon"): stage 2 is
 *  the AXP2101 soft POWER-OFF - < 40 uA with the RTC clock alive - and the
 *  PWR key (or USB) boots the tank, where progression_boot lives the whole
 *  stretch through. Deep sleep is now only the director's timed measurement
 *  window and the no-PMIC fallback; there the digital pads are HELD (the
 *  I2S + amp lines driven low, gpio_deep_sleep_hold_en), which is also what
 *  makes esp-idf isolate every other digital pad: un-isolated, deep sleep
 *  drew ~15 mA by the batlog, three times the light-sleep drowse it replaced. */
#define SLEEP_GRACE_US    (20LL * 60 * 1000000)  /* 2026-09-16: was 90 s; see the note above */
#define DIRECTOR_GRACE_US (5LL * 1000000)     /* `deepsleep N`: straight to stage 2 */
#define KEY_POLL_US       (1000000LL)         /* the grace wakes once a second to ask the PMIC about the PWR key */
static int battery_pct(void) { float f; bool c; return battery_port_read(&f, &c) ? (int)(f * 100 + 0.5f) : -1; }
typedef struct { float x, y, heading; uint8_t goal, valid; } fish_snap_t;
RTC_DATA_ATTR static fish_snap_t s_snap[N_FISH_MAX]; RTC_DATA_ATTR static int s_snap_n;
static void snap_log(const char *what) {          /* "FeZ 156,238/explore mira ..." */
    char line[N_FISH_MAX * 40] = ""; size_t l = 0;
    for (int i = 0; i < tank.n_fish && l + 40 < sizeof line; i++)
        l += snprintf(line + l, sizeof line - l, "%s%s %.0f,%.0f/%s", i ? " " : "", tank.fish[i].name,
                      tank.fish[i].x, tank.fish[i].y, GOAL_NAMES[tank.fish[i].goal.id]);
    ESP_LOGI(TAG, "%s: %s", what, line);
}
static void snapshot_fish(void) {
    s_snap_n = tank.n_fish;
    for (int i = 0; i < tank.n_fish; i++) {
        const fish_t *f = &tank.fish[i];
        s_snap[i] = (fish_snap_t){ f->x, f->y, f->heading, (uint8_t)f->goal.id, 1 };
    }
    snap_log("sleep snapshot");
}
static int restore_fish(void) {
    int n = 0;
    for (int i = 0; i < tank.n_fish && i < s_snap_n; i++) {
        const fish_snap_t *s = &s_snap[i];
        if (!s->valid || s->x < 0 || s->x > TANK_W || s->y < 0 || s->y > TANK_H) continue;
        fish_t *f = &tank.fish[i];
        f->x = s->x; f->y = s->y; f->heading = s->heading; tank_fish_face(f);
        if (s->goal < GOAL_COUNT) f->goal.id = (goal_id_t)s->goal;
        n++;
    }
    s_snap_n = 0;
    snap_log("wake restored");
    return n;
}
static void enter_sleep_for(int wake_after_s) {
    /* the face-down gesture's sleep: the IMU stays awake through the grace so
       turning the screen up can wake the tank (the CYD, no PMIC: no rails
       cycle around it, so the latch-up guard below has nothing to guard) */
    bool by_face = s_sleep_by_face; s_sleep_by_face = false;
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
    /* any sleep that starts face down - the gesture's, or BOOT pressed while
       it lies there - wakes when it is turned face up. A BOOT sleep face up
       keeps BOOT as its only wake: "not face down" would be true at once. */
    if (!by_face && orientation_face_sleep() && imu_port_face_down_now() == 1) by_face = true;
#endif
    int pct0 = battery_pct(), mv0 = battery_port_vbat_mv();
    int64_t grace_us = wake_after_s > 0 ? DIRECTOR_GRACE_US : SLEEP_GRACE_US;
    ESP_LOGI(TAG, "sleep: save, panel off, %d s grace then %s | battery %d%% %d mV",
             (int)(grace_us / 1000000),
             wake_after_s > 0 ? "deep sleep with the timer" : s_pmic ? "PMIC power-off (the PWR key boots it)" : "deep sleep (BOOT wakes)", pct0, mv0);
    touch_port_confirm_answer(-1);              /* an open reset prompt is a NO */
    if (!progression_save(&tank))               /* never cancels: a tank that can't save (NVS down, a save that
                                                   wouldn't load) must still sleep, or the key goes dead */
        ESP_LOGE(TAG, "sleep: the tank save failed - sleeping anyway, the last good save stands");
    bat_hist_save();                            /* the screen-on time so far */
    snapshot_fish();
    audio_port_sleep();        /* amp low, codec down, rail off - before the rails cycle */
    batlog_add(pct0, mv0, display_port_brightness(), true, "sleep");
    if (!by_face) imu_port_sleep();   /* quiesce BEFORE the rails cycle (latch-up guard) */
    display_port_sleep();
    while (!gpio_get_level(BTN_SLEEP)) vTaskDelay(pdMS_TO_TICKS(10));   /* wake triggers are level-low: never arm them held */
    vTaskDelay(pdMS_TO_TICKS(30));
    /* stage 1: the grace, RAM alive - light sleep in 1 s slices, each wake a
       one-byte I2C read of the PMIC's IRQ status for the PWR key (no IRQ line
       to the chip is needed); BOOT (gpio, level low) wakes it too for the
       director's bench and a board with no PMIC */
    int64_t t0 = esp_timer_get_time();
    bool pressed = false;
    for (;;) {
        int64_t left = grace_us - (esp_timer_get_time() - t0);
        if (left <= 0) break;
        gpio_wakeup_enable(BTN_SLEEP, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_sleep_enable_timer_wakeup(left < KEY_POLL_US ? left : KEY_POLL_US);
        esp_light_sleep_start();
        esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
        gpio_wakeup_disable(BTN_SLEEP);
        /* wake sources are STICKY in ESP-IDF (s_config.wakeup_triggers): the
         * grace's timer would otherwise follow us into stage 2 and boot the
         * tank 90 s later - which it did (2026-09-14: every sleep since the
         * two-stage change lasted exactly 3 minutes). Drop everything each
         * slice, then arm stage 2's own. */
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        if (why == ESP_SLEEP_WAKEUP_GPIO || battery_port_key_poll()) { pressed = true; break; }
        /* a face-down sleep wakes when the screen is no longer face down -
           turned up, or picked up. No answer from the IMU keeps it asleep:
           a bus hiccup must not wake the tank. */
        if (by_face && imu_port_face_down_now() == 0) {
            ESP_LOGI(TAG, "no longer face down: waking");
            pressed = true; break;
        }
    }
    if (pressed) {                              /* a quick wake: resume in place */
        while (!gpio_get_level(BTN_SLEEP)) vTaskDelay(pdMS_TO_TICKS(10));   /* a BOOT wake press, still down */
        float napped = (esp_timer_get_time() - t0) / 1e6f;
        tank_tick_sleep(&tank, napped);         /* the nap counts, tiny as it is */
        progression_woke(&tank);                /* a fry that was on its way: born now (2026-09-24) */
        battery_woke(&s_bh);                    /* what the gauge lost asleep is no screen-on drain */
        display_port_wake();
        imu_port_wake();
        batlog_add(battery_pct(), battery_port_vbat_mv(), 0, true, "nap");
        s_snap_n = 0; s_btn_armed = false; s_btn_low_since = 0;   /* require a fresh press */
        ESP_LOGI(TAG, "wake within the grace: resumed in place after %.0f s", napped);
        return;
    }
    /* stage 2: the grace passed. The save (written before the grace) plus the
       RTC clock cover the whole dark stretch at the next boot. The IMU kept
       awake for a face-down wake sleeps now: deep sleep cannot hear it. */
    if (by_face) imu_port_sleep();
    if (wake_after_s <= 0 && s_pmic) {
        batlog_add(battery_pct(), battery_port_vbat_mv(), 0, true, "off");   /* mirrored to NVS: the morning reads it back */
        ESP_LOGI(TAG, "grace over: PMIC power-off (the PWR key or USB boots the tank; the night is lived through at that boot)");
        vTaskDelay(pdMS_TO_TICKS(20));
        if (battery_port_poweroff()) vTaskDelay(pdMS_TO_TICKS(1000));      /* the rails drop here */
        ESP_LOGW(TAG, "still powered after the soft cut: deep sleep instead");
    }
    deep_sleep_now(wake_after_s);
}
static void enter_sleep(void) { enter_sleep_for(0); }
/* deep sleep: BOOT as ext0 (and the director's timer). The pads: I2S + amp
   CTRL low and held; with digital hold on, esp_deep_sleep_start isolates every
   other digital pad (input, output and pulls off) - the light-sleep drowse
   kept them driven, the first deep-sleep build left them neither, and the
   board around the chip drew ~15 mA all night (2026-09-15/16). */
static void deep_sleep_now(int wake_after_s) {
    ESP_LOGI(TAG, "deep sleep (BOOT wakes%s)", wake_after_s > 0 ? ", or the timer" : "");
    rtc_gpio_pullup_en(BTN_SLEEP); rtc_gpio_pulldown_dis(BTN_SLEEP);
    esp_sleep_enable_ext0_wakeup(BTN_SLEEP, 0);
    if (wake_after_s > 0) esp_sleep_enable_timer_wakeup((int64_t)wake_after_s * 1000000);
    audio_port_deep_sleep_pins();
    gpio_deep_sleep_hold_en();
    ESP_LOGI(TAG, "digital pads held + isolated");
    esp_deep_sleep_start();
}
void device_sleep(int wake_after_s) { enter_sleep_for(wake_after_s); }   /* director `deepsleep N` */

/* the PWR key held 1.5 s: save and cut NOW, no grace (the same power-off the
 * grace ends in). No PMIC: deep sleep. */
static void enter_poweroff(void) {
    ESP_LOGI(TAG, "power-off now: saving tank, PMIC soft cut (the PWR key boots)");
    touch_port_confirm_answer(-1);
    if (!progression_save(&tank))               /* never cancels (see enter_sleep_for) */
        ESP_LOGE(TAG, "power-off: the tank save failed - cutting anyway, the last good save stands");
    bat_hist_save();
    audio_port_sleep();
    batlog_add(battery_pct(), battery_port_vbat_mv(), display_port_brightness(), true, "off");   /* to NVS too: the shelf time is measurable at the next boot */
    imu_port_sleep();
    display_port_sleep();
    vTaskDelay(pdMS_TO_TICKS(50));
    if (battery_port_poweroff()) vTaskDelay(pdMS_TO_TICKS(1000));  /* rails drop here */
    deep_sleep_now(0);   /* no PMIC (QEMU / bring-up) or write failed */
}
void device_poweroff(void) { enter_poweroff(); }   /* director `poweroff` */

/* the PWR key, asked of the PMIC ten times a second: a short press sleeps
 * (the grace, then power-off), 1.5 s powers off at once. The boot's first
 * seconds are deaf to it - the press that powered the board on can still be
 * landing in the status register. */
#define KEY_BOOT_DEAF_US 3000000
static void pwr_key_poll(int64_t now) {
    static int64_t last;
    if (now - last < 100000) return;
    last = now;
    int k = battery_port_key_poll();
    if (!k) return;
    if (now < KEY_BOOT_DEAF_US) { ESP_LOGI(TAG, "PWR key %s press in the boot's first seconds: the power-on press, ignored", k == 2 ? "long" : "short"); return; }
    ESP_LOGI(TAG, "PWR key: %s press", k == 2 ? "long" : "short");
    if (k == 2) enter_poweroff(); else enter_sleep();
}

/* BOOT: the RESET chord (2026-09-11) - while it is held, a finger landing on
 * the glass opens the confirm prompt; a finger already resting there doesn't
 * count. With no PMIC (so no PWR key) a short press, at RELEASE, is the sleep
 * key as it was until 2026-09-16; a chord press never is. */
#define BTN_DEBOUNCE_US 50000
static void sleep_button_poll(int64_t now) {
    if (gpio_get_level(BTN_SLEEP)) {
        if (!s_pmic && s_btn_armed && s_btn_low_since && !s_btn_used && now - s_btn_low_since >= BTN_DEBOUNCE_US)
            enter_sleep();
        s_btn_armed = true; s_btn_low_since = 0; s_btn_used = false;
    } else if (s_btn_armed) {
        if (!s_btn_low_since) s_btn_low_since = now;
        else if (!s_btn_used && touch_port_pressed_since(s_btn_low_since) && !touch_port_confirm_up()) {
            s_btn_used = true;
            ESP_LOGI(TAG, "BOOT + tap: reset prompt");
            touch_port_confirm_open();
        }
    }
}

/* the keeper said YES: every saved tank goes - the live one and a director-
 * parked copy alike - and a fresh pair of fry takes the glass, saved at once
 * so a reboot lands on them (progression_reset) */
/* ---- sound (docs/AUDIO.md): the tank's moments -> cues. The listener only
 * enqueues (audio_port_play takes a mutex for a few microseconds); the
 * player task on core 1 does the rest. ---- */
static int stage_pitch(int fish) {                 /* fry high, elder low */
    if (fish < 0 || fish >= tank.n_fish) return AUDIO_PITCH_ONE;
    static const int p[4] = { 320, 282, 256, 230 };
    return p[tank.fish[fish].stage & 3];
}
static void on_tank_event(int ev, int fish, void *ud) {
    (void)ud;
    switch (ev) {
    case TEV_TAP:         audio_port_play(SND_TAP, AUDIO_PITCH_ONE); break;
    case TEV_FEED:        audio_port_play(SND_FEED, AUDIO_PITCH_ONE); break;
    case TEV_LIGHT_ON:    audio_port_play(SND_LIGHT_ON, AUDIO_PITCH_ONE); break;
    case TEV_LIGHT_OFF:   audio_port_play(SND_LIGHT_OFF, AUDIO_PITCH_ONE); break;
    case TEV_WIPE:        audio_port_play(SND_WIPE, AUDIO_PITCH_ONE); break;
    case TEV_SNIP:        audio_port_play(SND_SNIP, AUDIO_PITCH_ONE); break;
    case TEV_EAT:         audio_port_play(SND_EAT, stage_pitch(fish)); break;
    case TEV_SPOOK:       audio_port_play(SND_SPOOK, AUDIO_PITCH_ONE); break;
    case TEV_INVESTIGATE: audio_port_play(SND_INVESTIGATE, stage_pitch(fish)); break;
    case TEV_BUBBLES:     audio_port_play(SND_BUBBLES, AUDIO_PITCH_ONE); break;
    case TEV_WELCOME:     audio_port_play(SND_WELCOME, AUDIO_PITCH_ONE); break;
    case TEV_WHEEL_TICK:  audio_port_play(SND_WHEEL_TICK, AUDIO_PITCH_ONE); break;
    case TEV_CONFIRM:     audio_port_play(SND_CONFIRM, AUDIO_PITCH_ONE); break;
    default: break;
    }
}
/* the gauge, once a second, for the pill and the low-battery rule
 * (docs/AUDIO.md 4a): at 10% and off the cable the notice + cue fire once;
 * the pill then stays on screen until the cable is seen or the gauge has
 * read above 10% for 30 s. Since 2026-09-24 it also feeds the battery
 * page's history (battery.h) and, when the cable goes in, shows the pill -
 * bolt and sweep - for BAT_POPUP_S: plugging in answers on the glass. */
#define LOW_BATTERY_FRAC 0.10f
static float s_bat_frac; static bool s_bat_chg, s_bat_ok, s_bat_low;
static int s_bat_state = BAT_ON_BATTERY, s_bat_mv;   /* BAT_*; VBAT, read with the gauge (the page shows it) */
static int64_t s_bat_popup_us;              /* the cable went in: the pill shows until then */
static int s_bat_fake = -1, s_bat_fake_state;   /* director `battery N [charging|full|plugged]`: a staged gauge, for the camera (-1 = the real one) */
void device_fake_battery(int pct, int state) {
    s_bat_fake = pct < 0 ? -1 : pct > 100 ? 100 : pct; s_bat_fake_state = state;
    if (pct < 0) s_bat_low = false;
}
static void bat_hist_load(void) {
    nvs_handle_t h; bat_hist_t b; size_t len = sizeof b; bool ok = false;
    if (nvs_open("bat", NVS_READONLY, &h) == ESP_OK) { ok = nvs_get_blob(h, "hist", &b, &len) == ESP_OK && len == sizeof b; nvs_close(h); }
    battery_init(&s_bh, ok ? &b : NULL);
    if (ok) ESP_LOGI(TAG, "battery history: %s since %lld, screen on %u min, drain %s%.1f %%/h, charge %s%.1f %%/h",
                     b.on_power ? "on the cable" : "on battery", (long long)b.since_unix, (unsigned)(b.awake_s / 60),
                     b.drain_x10 ? "" : "(default) ", b.drain_x10 ? b.drain_x10 / 10.0 : BAT_DRAIN_DEFAULT,
                     b.charge_x10 ? "" : "(default) ", b.charge_x10 ? b.charge_x10 / 10.0 : BAT_CHARGE_DEFAULT);
}
static void bat_hist_save(void) {
    nvs_handle_t h; if (nvs_open("bat", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, "hist", &s_bh.h, sizeof s_bh.h) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}
void device_battery_log(void) {             /* director `state` / `battery` */
    bat_info_t bi; char a[16], b[16], c[16];
    battery_info(&s_bh, clock_port_now_unix(), s_bat_ok ? (int)(s_bat_frac * 100 + 0.5f) : -1, s_bat_mv, s_bat_state, &bi);
    battery_fmt_dur(a, sizeof a, bi.since_min); battery_fmt_dur(b, sizeof b, bi.awake_min); battery_fmt_dur(c, sizeof c, bi.left_min);
    ESP_LOGI(TAG, "battery page: %d%% %s%s | since the cable moved %s (at %d%%), screen on %s | %s %s | a full charge ~%d min (drain %s%.1f %%/h, charge %s%.1f %%/h)",
             bi.pct, bi.state == BAT_CHARGING ? "CHARGING" : bi.state == BAT_FULL ? "FULL" : bi.state == BAT_PLUGGED ? "PLUGGED (not charging)" : "ON BATTERY",
             s_bat_fake >= 0 ? " (STAGED)" : "", a, s_bh.h.since_pct, b, bi.state == BAT_CHARGING ? "full in" : "left", c, bi.life_min,
             s_bh.h.drain_x10 ? "" : "default ", s_bh.h.drain_x10 ? s_bh.h.drain_x10 / 10.0 : BAT_DRAIN_DEFAULT,
             s_bh.h.charge_x10 ? "" : "default ", s_bh.h.charge_x10 ? s_bh.h.charge_x10 / 10.0 : BAT_CHARGE_DEFAULT);
}
static void battery_frame(int64_t now) {
    static int64_t bat_us, above_since; static bool was_power;
    if (now - bat_us < 1000000) return;
    float dt = bat_us ? (now - bat_us) / 1e6f : 0;
    if (dt > 2) dt = 2;                         /* a nap in the sleep grace is no screen time */
    bat_us = now;
    s_bat_ok = battery_port_read(&s_bat_frac, &s_bat_chg);
    s_bat_state = s_bat_ok ? battery_port_state() : BAT_ON_BATTERY;
    if (s_bat_fake >= 0) { s_bat_ok = true; s_bat_frac = s_bat_fake / 100.0f; s_bat_state = s_bat_fake_state; }   /* staged: that level, that cable, whatever the real one says */
    s_bat_chg = s_bat_state == BAT_CHARGING;
    if (!s_bat_ok) return;
    s_bat_mv = battery_port_vbat_mv();
    int pct = (int)(s_bat_frac * 100 + 0.5f), edge;
    if (s_bat_fake < 0) edge = battery_tick(&s_bh, clock_port_now_unix(), dt, pct, s_bat_state);   /* a staged gauge teaches the history nothing */
    else edge = BAT_ON_POWER(s_bat_state) == was_power ? 0 : BAT_ON_POWER(s_bat_state) ? 1 : -1;
    was_power = BAT_ON_POWER(s_bat_state);
    if (edge > 0) { s_bat_popup_us = now + BAT_POPUP_S * 1000000LL;
                    ESP_LOGI(TAG, "battery: cable in at %d%% (%s) - the pill shows %d s", pct, s_bat_state == BAT_CHARGING ? "charging" : s_bat_state == BAT_FULL ? "full" : "not charging", BAT_POPUP_S); }
    else if (edge < 0) ESP_LOGI(TAG, "battery: unplugged at %d%%", pct);
    if (battery_take_save(&s_bh)) bat_hist_save();
    if (!s_bat_low) {
        if (!BAT_ON_POWER(s_bat_state) && s_bat_frac <= LOW_BATTERY_FRAC) {
            s_bat_low = true; above_since = 0; notice_low_battery();
            ESP_LOGW(TAG, "battery low: %d%% - notice + pill", (int)(s_bat_frac * 100 + 0.5f));
        }
    } else if (BAT_ON_POWER(s_bat_state)) { s_bat_low = false; ESP_LOGI(TAG, "battery: on the cable, the low pill down"); }
    else if (s_bat_frac > LOW_BATTERY_FRAC) {
        if (!above_since) above_since = now;
        else if (now - above_since > 30LL * 1000000) { s_bat_low = false; ESP_LOGI(TAG, "battery back above %d%%: pill down", (int)(LOW_BATTERY_FRAC * 100)); }
    } else above_since = 0;
}

static void reset_tank(void) {
    ESP_LOGW(TAG, "RESET: wiping the tank (%d fish) for a fresh one", tank.n_fish);
    progression_reset(&tank, (uint32_t)esp_timer_get_time() ^ 0xC0FFEEu);
    notice_sync(&tank);                         /* a fresh tank has nothing to announce */
    brightness_save();                          /* the erase took the setting with it */
    orientation_save();                         /* ... and this one */
    ESP_LOGI(TAG, "fresh tank: %s + %s, both fry", tank.fish[0].name, tank.fish[1].name);
    setup_begin(&tank);                         /* welcome, names, colours - as on a fresh install */
}

static void tank_task(void *arg) {
    (void)arg;
    int64_t last = esp_timer_get_time(); int cur = 0;
    int64_t last_log = last;
    int64_t render_us = 0, flush_us = 0, card_us = 0; uint32_t frames = 0, card_frames = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f; last = now; if (dt > 0.25f) dt = 0.25f;
        sleep_button_poll(now);
        pwr_key_poll(now);
        imu_port_poll(now);
        if (imu_port_moving()) audio_port_prewarm();   /* in a hand: the codec stays warm (docs/AUDIO.md) */
        if (imu_port_handled()) tank_handled(&tank);   /* ... and the light stays on (two polls of motion: a bump on the desk is not a pick-up) */
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
        /* screen down and still for 2 s: the sleep key, if the keeper allows
           it. Taken either way, so switching it on later fires nothing stale. */
        if (imu_port_take_face_down() && orientation_face_sleep()) {
            ESP_LOGI(TAG, "face down: sleeping (face up wakes it within the grace)");
            s_sleep_by_face = true;
            enter_sleep();
        }
        /* with an IMU answering, the IMU turns the picture and the settings
           row is FACE DOWN, not SCREEN: a saved SCREEN choice is set aside */
        bool inv = s_imu ? imu_port_inverted() : orientation_flipped();
#else
        bool inv = imu_port_inverted() != orientation_flipped();   /* the IMU's flip, turned again by the keeper's SCREEN choice */
#endif
        display_port_set_inverted(inv);   /* per-frame, so a flip lands between flushes */
        touch_port_set_inverted(inv);
        touch_port_poll(&tank);
        director_poll(&tank);
        int ans = touch_port_confirm_take();
        if (ans > 0) reset_tank();
        else if (ans < 0) ESP_LOGI(TAG, "reset prompt: tank kept");
        { int v = 0, w = touch_port_take_setting(&v);                 /* the settings page */
          if (w == SET_TAP_BRIGHT) brightness_set_level(v);
          else if (w == SET_TAP_VOLUME) { audio_port_set_volume(v); if (v) audio_port_play(SND_CONFIRM, AUDIO_PITCH_ONE); }
          else if (w == SET_TAP_LIGHT) ESP_LOGI(TAG, "settings: lights out %s", v ? "AUTO (the idle rule)" : "MANUAL (double-tap the glass)");
          else if (w == SET_TAP_IDLE) ESP_LOGI(TAG, "settings: lights out after %d s still", v);
          else if (w == SET_TAP_FLIP) orientation_set(v != 0);
          else if (w == SET_TAP_FACE) orientation_set_face_sleep(v != 0); }
        { int r = touch_port_take_shop();                               /* the shop's UNLOCK / MOVE / SELL */
          if (r >= SHOP_TAP_SELL) {                                     /* sold back: the refund, the piece gone, the row for sale again */
              int item = r - SHOP_TAP_SELL;
              if (progression_sell(&tank, item)) { audio_port_play(SND_CONFIRM, AUDIO_PITCH_ONE);
                  ESP_LOGI(TAG, "shop: %s sold back for %d, balance %d", SD_ITEMS[item].name, progression_sell_value(item), (int)tank.sd_balance); }
          } else if (r >= SHOP_TAP_MOVE) {                              /* a piece already in the tank: place it again */
              int item = r - SHOP_TAP_MOVE;
              touch_port_show_shop(false); setup_begin_place(&tank, item);
              ESP_LOGI(TAG, "shop: MOVE %s - placement page up (drag, DEPTH, DONE)", SD_ITEMS[item].name);
          } else if (r >= SHOP_TAP_BUY) {
              int item = r - SHOP_TAP_BUY;
              if (progression_buy(&tank, item)) { audio_port_play(SND_CONFIRM, AUDIO_PITCH_ONE);
                  ESP_LOGI(TAG, "shop: %s unlocked, %d sand dollars left", SD_ITEMS[item].name, (int)tank.sd_balance);
                  if (tank_decor_placeable(item)) {                     /* a placeable piece: the page opens over the live tank */
                      touch_port_show_shop(false); setup_begin_place(&tank, item);
                      ESP_LOGI(TAG, "shop: placement page up for the %s", SD_ITEMS[item].name); } }
              else ESP_LOGI(TAG, "shop: %s refused (balance %d, price %d)", SD_ITEMS[item].name, (int)tank.sd_balance, SD_ITEMS[item].price); } }
        brightness_apply(tank.night);
        { static int64_t last_bat; if (now - last_bat > 5LL * 60 * 1000000) {   /* battery log: awake sample every 5 min */
            batlog_add(battery_pct(), battery_port_vbat_mv(), display_port_brightness(), false, last_bat ? "" : "boot"); last_bat = now; } }
        tank.hold_light = setup_active() || touch_port_confirm_up();   /* no lights-out mid-name */
        tank.ui_cover = tank.hold_light || touch_port_milestones() || touch_port_settings() || touch_port_shop() || touch_port_battery();   /* a fry's spawning waits */
        tank_tick(&tank, dt, llm_ok ? advisor_llm_esp : advisor_rules);
        progression_tick(&tank, dt);
        battery_frame(now);
        notice_tick(&tank, dt, tank.ui_cover || setup_birth_due());   /* a fry's welcome first (2026-09-29) */
        { int cue = notice_take_cue(); if (cue >= 0) audio_port_play(cue, AUDIO_PITCH_ONE); }
        audio_port_set_night(tank.night);
        { static bool loop_on;                     /* the bubble loop rides the setup's placement page */
          bool loop = setup_active() && !setup_is_birth() && setup_page() == SETUP_PG_BUBBLES;
          if (loop != loop_on) { if (loop) audio_port_play(SND_BUBBLES_LOOP, AUDIO_PITCH_ONE); else audio_port_stop(SND_BUBBLES_LOOP); loop_on = loop; } }
        { static int prev_sel = -1; int s = touch_port_selected();   /* the stats card coming and going */
          if (s >= 0 && prev_sel < 0) audio_port_play(SND_CARD_OPEN, AUDIO_PITCH_ONE);
          if (s < 0 && prev_sel >= 0) audio_port_play(SND_CARD_CLOSE, AUDIO_PITCH_ONE);
          prev_sel = s; }
        if (!touch_port_confirm_up()) {           /* an arrival owed its welcome: the birth flow (setup.c) */
            int nb = setup_poll_birth(&tank);
            if (nb >= 0) { touch_port_dismiss(); audio_port_play(SND_ARRIVAL, AUDIO_PITCH_ONE);
                           ESP_LOGI(TAG, "a new fry, %s: birth flow up (announce, name, family; director `setup off` drops it)", tank.fish[nb].name); }
        }
        if (fb[cur]) {
            if (s_prefetch_pending) {                       /* prior frame's scene prefetch */
                xSemaphoreTake(s_amc_done, portMAX_DELAY);
                s_prefetch_pending = false;
                render_fb_primed(s_prefetch_fb, s_prefetch_ep);
            }
            int64_t t0 = esp_timer_get_time();
            render_tank(&tank, fb[cur], TANK_W);
            touch_port_poll(&tank);          /* the CST816 is polled, not interrupt-
                                                driven: extra samples inside the frame
                                                keep quick finger taps from slipping
                                                between 40 ms frame boundaries */
            int sel = touch_port_selected();
            int64_t tc = esp_timer_get_time();
            if (touch_port_milestones()) {       /* milestones page: covers the tank until a tap */
                render_milestones(&tank, fb[cur], TANK_W);
                sel = -1;
            } else if (touch_port_settings()) {  /* settings page: brightness + volume */
                render_settings_set_flip(orientation_flipped());
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
                render_settings_set_imu(s_imu, orientation_face_sleep());
#endif
                render_settings(&tank, fb[cur], TANK_W, brightness_level(), audio_port_volume());
                sel = -1;
            } else if (touch_port_shop()) {      /* the shop: sand dollars and what they buy */
                render_shop(&tank, fb[cur], TANK_W);
                sel = -1;
            } else render_sd_toast(&tank, fb[cur], TANK_W);   /* the live tank: "+N" as dollars are earned */
            if (sel >= 0) render_stats_card(&tank, sel, fb[cur], TANK_W);   /* tapped fish: stats card (+ the pill) */
            {   /* the battery pill: with a card, while low, and a few seconds after the cable goes in;
                   the battery page (a tap on the pill) over the live tank in its place */
                bool pages = touch_port_milestones() || touch_port_settings() || touch_port_shop() || setup_active() || touch_port_confirm_up();
                bool bpage = touch_port_battery() && s_bat_ok && !pages;
                bool pill = s_bat_ok && !pages && !bpage && (sel >= 0 || s_bat_low || now < s_bat_popup_us);
                if (bpage) {
                    bat_info_t bi;
                    battery_info(&s_bh, clock_port_now_unix(), (int)(s_bat_frac * 100 + 0.5f), s_bat_mv, s_bat_state, &bi);
                    render_battery_info(fb[cur], TANK_W, &bi, tank.clock);
                } else if (pill) render_battery(fb[cur], TANK_W, s_bat_frac, s_bat_state, tank.clock);
                touch_port_set_pill(pill);
            }
            if (!touch_port_milestones() && !touch_port_settings() && !touch_port_shop()) {   /* an announcement over the live tank */
                const notice_t *nt = notice_current();
                if (nt) render_notice(&tank, fb[cur], TANK_W, nt->kind, nt->fish, nt->bit, 1.0f - nt->age / NOTICE_UP_S);
            }
            if (setup_active())                  /* first-run setup: over the tank, under the prompt */
                render_setup(&tank, fb[cur], TANK_W, tank.clock);
            if (touch_port_confirm_up())         /* reset prompt: over everything, fish still swim */
                render_confirm_reset(fb[cur], TANK_W, touch_port_confirm_frac());
            int64_t t1 = esp_timer_get_time();
            if (sel >= 0) { card_us += t1 - tc; card_frames++; }
            display_port_flush(fb[cur]);
            touch_port_poll(&tank);
            render_us += t1 - t0; flush_us += esp_timer_get_time() - t1; frames++;
            uint16_t *next = fb[cur ^ 1];
            const uint16_t *scene = render_scene_buf(&s_prefetch_ep);
            if (s_amc && scene && next && next != fb[cur] &&
                esp_async_memcpy(s_amc, next, (void *)scene, PLAN_FB_BYTES, amc_cb, NULL) == ESP_OK) {
                s_prefetch_fb = next; s_prefetch_pending = true;
            }
        }
        cur ^= 1;
        if (now - last_log > 10 * 1000000) {
            if (frames) {
                unsigned ep; const uint16_t *sc = render_scene_buf(&ep);
                ESP_LOGI("display", "fb mid 0x%04x corner 0x%04x | scene mid 0x%04x ep %u | night %d prefetch %d",
                         fb[cur] ? fb[cur][(TANK_H / 2) * TANK_W + TANK_W / 2] : 0,
                         fb[cur] ? fb[cur][5 * TANK_W + 5] : 0,
                         sc ? sc[(TANK_H / 2) * TANK_W + TANK_W / 2] : 0, ep,
                         (int)tank.night, (int)s_prefetch_pending);
                ESP_LOGI("display", "%.1f fps | render %.1f ms flush %.1f ms | scene %.1f shafts %.1f veg %.1f fd/bub %.1f fish %.1f vig %.1f algae %.1f | card %.1f ms x%lu | veg %.2f %.2f %.2f",
                         frames * 1e6f / (float)(now - last_log),
                         render_us / 1e3f / frames, flush_us / 1e3f / frames,
                         render_prof_us[0] / 1e3f / frames, render_prof_us[1] / 1e3f / frames,
                         render_prof_us[2] / 1e3f / frames, render_prof_us[3] / 1e3f / frames,
                         render_prof_us[4] / 1e3f / frames, render_prof_us[5] / 1e3f / frames,
                         render_prof_us[6] / 1e3f / frames,
                         card_frames ? card_us / 1e3f / card_frames : 0.0f, (unsigned long)card_frames,
                         tank.veg_growth[0], tank.veg_growth[1], tank.veg_growth[2]);
                card_us = 0; card_frames = 0;
                memset(render_prof_us, 0, sizeof render_prof_us);
            }
            render_us = flush_us = 0; frames = 0;
            uint32_t d, ms; float tps; advisor_llm_esp_stats(&d, &ms, &tps);
            char goals[N_FISH_MAX * 16] = ""; size_t gl = 0;
            for (int i = 0; i < tank.n_fish && gl + 16 < sizeof goals; i++)
                gl += snprintf(goals + gl, sizeof goals - gl, "%s%s", i ? " " : "", GOAL_NAMES[tank.fish[i].goal.id]);
            ESP_LOGI(TAG, "t=%.0fs %d fish goals: %s | asks %lu decisions %lu last %lu ms %.1f tok/s | starve-ignored %d | heap int %u KB psram %u KB | battery %d%% %d mV bright %d",
                     tank.clock, tank.n_fish, goals, (unsigned long)tank.advisor_asks,
                     (unsigned long)d, (unsigned long)ms, tps, tank_reflex_overrides,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
                     battery_pct(), battery_port_vbat_mv(), display_port_brightness());
            last_log = now;
        }
        int spent_ms = (int)((esp_timer_get_time() - now) / 1000);
        int rest = 16 - spent_ms;                /* pace toward 60 fps, always yield >= 1 tick */
        vTaskDelay(pdMS_TO_TICKS(rest < 1 ? 1 : rest));
    }
}

/* the settings page's dim version line: ESP-IDF stamps the app descriptor
   with `git describe --always --tags --dirty` of the checkout at build (the
   installer's Actions job checks out with the full history), the same words
   the installer page shows for what it would write */
const char *version_port_string(void) { return esp_app_get_description()->version; }

/* NVS holds the keeper's tank, so it is never erased on a guess. Only "no
 * free pages" (a partition that can't mount at all) is answered with an
 * erase, and a raw copy of the partition goes to the unused front of
 * "storage" first, so the tank can still be pulled out by hand
 * (esptool read_flash 0xA90000 0x6000 rescue.bin). Any other error - a
 * newer IDF's format after a rollback, say - leaves the flash alone: the tank
 * runs without saving until a build that can read it is back. */
#define NVS_RESCUE_SIZE 0x6000
static void nvs_start(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_OK) return;
    if (e != ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(TAG, "NVS init: %s - running WITHOUT saving; the saved tank is left as it is", esp_err_to_name(e));
        return;
    }
    const esp_partition_t *nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    const esp_partition_t *st  = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    uint8_t *raw = nvs && nvs->size <= NVS_RESCUE_SIZE ? heap_caps_malloc(nvs->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    bool kept = raw && st && st->size >= NVS_RESCUE_SIZE
             && esp_partition_read(nvs, 0, raw, nvs->size) == ESP_OK
             && esp_partition_erase_range(st, 0, NVS_RESCUE_SIZE) == ESP_OK
             && esp_partition_write(st, 0, raw, nvs->size) == ESP_OK;
    free(raw);
    if (!kept) {
        ESP_LOGE(TAG, "NVS init: no free pages and no rescue copy - running WITHOUT saving, nothing erased");
        return;
    }
    ESP_LOGE(TAG, "NVS init: no free pages - raw copy at storage+0 (0x%lx), erasing NVS", (unsigned long)st->address);
    nvs_flash_erase();
    nvs_flash_init();
}

void app_main(void) {
    ESP_LOGI(TAG, "pocket-tank v%s %s (build %s) boot%s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string(),
             esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 ? " (woken by button)" : "");
    gpio_config_t btn = { .pin_bit_mask = 1ULL << BTN_SLEEP, .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&btn);
    gpio_deep_sleep_hold_dis();              /* a deep-sleep wake is a boot: the night's pad holds end here */
    nvs_start();
    { int carried = batlog_init();       /* the battery log survives every reset but a power-on */
      if (carried) ESP_LOGI(TAG, "batlog: %d samples carried through the reset (director `batlog` reads them)", carried); }
    brightness_init();
    orientation_init();
    bat_hist_load();
    assert_plan();
    for (int i = 0; i < PLAN_FB_COUNT; i++) {
        fb[i] = heap_caps_aligned_alloc(64, PLAN_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!fb[i]) fb[i] = i ? fb[0] : NULL;          /* no PSRAM: share or skip */
    }
    if (!fb[0]) ESP_LOGW(TAG, "no framebuffer RAM: rendering disabled (tank still runs)");
    /* static-scene cache: gradient/pebbles/reef drawn once per lighting state */
    uint16_t *scene = heap_caps_aligned_alloc(64, PLAN_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (scene) render_set_scene_cache(scene); else ESP_LOGW(TAG, "no scene cache RAM: full redraw per frame");
    uint8_t *vig = heap_caps_malloc(TANK_W * TANK_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (vig) render_set_vignette_cache(vig);
    /* dirty mask (20 KB): internal SRAM if it fits - it is cleared and read
       every frame, and in PSRAM that was ~1.5 ms; PSRAM fallback */
    { void *ds = heap_caps_malloc(render_decor_scratch_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   /* the coral / cluster tables: PSRAM, never .bss */
      if (ds) render_set_decor_scratch(ds); else ESP_LOGW(TAG, "decor scratch: no PSRAM (%u B) - falling back to internal", (unsigned)render_decor_scratch_size()); }
    uint32_t *dirty = heap_caps_malloc(RENDER_DIRTY_WORDS * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!dirty) dirty = heap_caps_malloc(RENDER_DIRTY_WORDS * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dirty) render_set_dirty_mask(dirty); else ESP_LOGW(TAG, "no dirty mask RAM: full redraw per frame");
    ESP_LOGI(TAG, "dirty mask %s", !dirty ? "none" : esp_ptr_external_ram(dirty) ? "PSRAM" : "internal SRAM");
    /* stats card cache: redrawn 4x/s, blitted otherwise. Internal SRAM if it
       fits (a PSRAM->PSRAM copy of the 56 KB sprite cost 3.3 ms per frame,
       more than the draw it replaced) */
    uint16_t *card = heap_caps_malloc(RENDER_CARD_W * RENDER_CARD_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!card) card = heap_caps_malloc(RENDER_CARD_W * RENDER_CARD_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (card) render_set_card_cache(card);
    ESP_LOGI(TAG, "card cache %s", !card ? "none" : esp_ptr_external_ram(card) ? "PSRAM" : "internal SRAM");
    /* model: mmap the raw partition; weights are read through the flash cache */
    const esp_partition_t *mp = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "model");
    if (!mp) { ESP_LOGE(TAG, "no model partition"); }
    else {
        const void *map; esp_partition_mmap_handle_t h;
        if (esp_partition_mmap(mp, 0, mp->size, ESP_PARTITION_MMAP_DATA, &map, &h) == ESP_OK) {
            llm_ok = advisor_llm_esp_init(map, mp->size, tokenizer_bin_start,
                                          tokenizer_bin_end - tokenizer_bin_start);
            ESP_LOGI(TAG, "model partition %u KB mmap'd, advisor %s", (unsigned)mp->size / 1024, llm_ok ? "LLM" : "rules (model missing)");
        } else ESP_LOGE(TAG, "model mmap failed");
    }
    render_clock_us = esp_timer_get_time;    /* per-stage frame profiling in the display log */
    display_port_init();
    touch_port_init();
    s_pmic = battery_port_init(board_i2c_bus());
    battery_port_trim_rails();        /* the schematic's unused outputs off (docs/HANDOFF.md, the battery pass) */
    battery_port_key_init();          /* the PWR key: sleep / power-off IRQs on, the power-on press cleared */
    codec_port_init(board_i2c_bus());  /* the ES8311 fully down until a cue needs it (its digital side shares VCC3V3) */
    audio_port_init(board_i2c_bus());  /* the sound bank + player task (docs/AUDIO.md); silent without the codec */
    tank_events_set(on_tank_event, NULL);
    s_imu = imu_port_init(board_i2c_bus());   /* screen auto-flip; absent IMU = always upright */
    director_init();                  /* serial scenario console (filming / bench) */
    /* scene-prefetch DMA: installed only AFTER the display grabbed its SPI DMA
       channel — installed earlier, async memcpy steals SPI2's GDMA trigger
       slot and the panel silently loses its pixel path (black screen). */
    if (scene && fb[0] && fb[1] != fb[0]) {
        async_memcpy_config_t amc_cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
        amc_cfg.backlog = 4; amc_cfg.sram_trans_align = 4; amc_cfg.psram_trans_align = 64;
        s_amc_done = xSemaphoreCreateBinary();
        if (esp_async_memcpy_install(&amc_cfg, &s_amc) != ESP_OK) {
            s_amc = NULL; ESP_LOGW(TAG, "async memcpy unavailable: CPU scene restore");
        }
    }
    rtc_port_init(board_i2c_bus());   /* wall clock for the ravenous rule */
    tank_init(&tank, (uint32_t)esp_timer_get_time() ^ 0xC0FFEEu);
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool from_sleep = cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_TIMER;
    if (from_sleep) {                        /* the night, lived through in one step */
        float h = progression_wake(&tank, clock_port_now_unix());
        ESP_LOGI(TAG, "wake from deep sleep (%s): %s%.1f h simulated | hunger[0] %.1f | battery %d%% %d mV",
                 cause == ESP_SLEEP_WAKEUP_TIMER ? "timer" : "BOOT", h < 0 ? "no clock, " : "", h < 0 ? 0.0f : h,
                 tank.n_fish ? tank.fish[0].hunger : 0.0f, battery_pct(), battery_port_vbat_mv());
        batlog_add(battery_pct(), battery_port_vbat_mv(), 0, true, "wake");
        int put_back = restore_fish();       /* where they fell asleep, on the goal they had */
        ESP_LOGI(TAG, "wake: %d of %d fish put back where they were", put_back, tank.n_fish);
    } else {                                 /* a cold boot - power-on, a flash, a PMIC power-off, a cell that died: the absence is lived through just the same (2026-09-16) */
        float h = progression_boot(&tank);
        ESP_LOGI(TAG, "cold boot: %s%.1f h lived through since the save | hunger[0] %.1f | battery %d%% %d mV",
                 h < 0 ? "no save or no clock, " : "", h < 0 ? 0.0f : h,
                 tank.n_fish ? tank.fish[0].hunger : 0.0f, battery_pct(), battery_port_vbat_mv());
    }
    notice_sync(&tank);                      /* what is already earned stays unannounced */
    { uint32_t r = progression_loaded_release();   /* which release wrote the tank we just loaded */
      if (r) ESP_LOGI(TAG, "the save was written by v%d.%d.%d", (int)(r >> 16), (int)(r >> 8 & 255), (int)(r & 255));
      else ESP_LOGI(TAG, "the save predates release numbers (or there was none)"); }
    ESP_LOGI(TAG, "population %d (cap %d): %s + %s ...", tank.n_fish, POP_CAP,
             tank.fish[0].name, tank.n_fish > 1 ? tank.fish[1].name : "-");
    if (progression_setup_pending()) {           /* a new tank (fresh install, or a reset mid-flow): the welcome */
        setup_begin(&tank);
        ESP_LOGI(TAG, "first-run setup: welcome, names, colours (director `setup off` drops it)");
    }
    /* one-shot: what a frame costs with the stats card up (the card only
       renders on a tap, so the running profile rarely shows it) */
    if (fb[0] && tank.n_fish > 0) {
        uint16_t *tmp = heap_caps_aligned_alloc(64, PLAN_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (tmp) {
            render_tank(&tank, tmp, TANK_W);
            int64_t t0 = esp_timer_get_time();
            render_stats_card(&tank, 0, tmp, TANK_W);          /* first: redraw into the cache */
            int64_t t1 = esp_timer_get_time();
            for (int i = 0; i < 4; i++) render_stats_card(&tank, 0, tmp, TANK_W);   /* then: blits */
            ESP_LOGI("display", "stats card: redraw %.1f ms, blit %.1f ms per frame",
                     (t1 - t0) / 1e3f, (esp_timer_get_time() - t1) / 4e3f);
            heap_caps_free(tmp);
        }
    }
    xTaskCreatePinnedToCore(tank_task, "tank", 12288, NULL, 4, NULL, 0);
}
