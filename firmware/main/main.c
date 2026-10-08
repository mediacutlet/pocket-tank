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
#include "update.h"
#include "update_mode.h"
#include "net_time.h"
#include <sys/time.h>
#include "model_trailer.h"
#include "esp_ota_ops.h"
#include "brightness.h"
#include "orientation.h"
#include "sleep_setting.h"                 /* (this fork) the 320x240 boards' SLEEP row: SLEEP_SETTING_ROW */
#include "batlog.h"
#include "codec_port.h"
#include "progression.h"
#include "audio_port.h"
#include "audio.h"
#include "notice.h"
#include "tank_events.h"
#include "setup.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_app_desc.h"
#include "version.h"
#include "rtc_port.h"
#include "driver/i2c_master.h"
#include "esp_async_memcpy.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#if CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
#include "driver/usb_serial_jtag.h"         /* usb_serial_jtag_is_connected: the dark stays awake for a USB host */
#include "board_pins.h"                     /* the outputs the dark holds through light sleep (dark_hold_pins) */
#endif

/* BOOT (GPIO0, active low, RTC-wake capable): the reset chord (held + a tap
 * on the glass), the director's deep-sleep wake, and the sleep key only on a
 * board with no PMIC. Since 2026-09-16 the sleep key is the PWR key (the
 * AXP2101's PWRON, polled over I2C): the night mode is a PMIC power-off,
 * from which ONLY that key (or USB) can bring the board back, so it has to
 * be the one key the keeper ever presses - press to sleep, press to wake. */
#define BTN_SLEEP GPIO_NUM_0
static bool s_pmic;                        /* an AXP2101 answered: the PWR key exists, power-off is real */
static bool s_rtc;                         /* an RTC chip answered: the wall clock survives a PMIC power-off. Without one
                                              (the round 1.75C) the night is a DEEP sleep - the ESP32's own clock keeps
                                              the time - and only the PWR key's long press cuts the power */
#include "board_pins.h"
#define PWR_SENSE ((gpio_num_t)board_pwr_sense_pin())   /* the round board and the watch: a line that is high while the PWR key is down */
static bool pwr_sensed(void) { return board_pwr_sense_pin() >= 0; }
static bool pwr_sense_down(void) { return pwr_sensed() && gpio_get_level(PWR_SENSE); }
static bool s_imu;                         /* an IMU answered: it turns the picture (and, on the CYD, the face-down gesture) */
static bool s_sleep_by_face;               /* the next sleep was the face-down gesture: face up wakes it */
static const char *s_sleep_why = "?";      /* what asked for the next sleep, for the none and dark modes' log lines */
/* the sleep mode (Kconfig POCKET_TANK_SLEEP_MODE): deepsleep is the two
 * stages below as they always were; none, screen and lightsleep turn off at
 * the top of enter_sleep_for (2026-10-08). An sdkconfig naming none of the
 * four is deepsleep too: idf.py does not re-read Kconfig for a build
 * directory that already has one, so an sdkconfig made before the choice
 * existed has no line for it, and must build the tank as it was. */
#if CONFIG_POCKET_TANK_SLEEP_NONE
#define SLEEP_MODE_NAME "none"
#elif CONFIG_POCKET_TANK_SLEEP_SCREEN
#define SLEEP_MODE_NAME "screen"
#elif CONFIG_POCKET_TANK_SLEEP_LIGHT
#define SLEEP_MODE_NAME "lightsleep"
#else
#define SLEEP_MODE_NAME "deepsleep"
#define SLEEP_MODE_DEEP 1
#endif
/* (this fork) the 320x240 boards' SLEEP row (sleep_setting.h, 2026-10-08):
 * on a CYD or a Touch-LCD-2 built for none, screen or lightsleep the mode is
 * the keeper's, chosen at run time, and the build's mode only its factory
 * default. The dark is compiled for every such build - one built for none
 * too, since SCREEN and LIGHT are a tap away - and the tests of the build's
 * mode below become tests of the choice. The name in the log lines is the
 * choice's. */
#ifdef SLEEP_SETTING_ROW
static const char *sleep_mode_name(void) {
    int choice = sleep_setting();
    if (choice == SET_SLEEP_NEVER) return "never";
    if (choice == SET_SLEEP_SCREEN) return "screen";
    return "lightsleep";
}
#undef  SLEEP_MODE_NAME
#define SLEEP_MODE_NAME sleep_mode_name()
#endif
#if defined(CONFIG_POCKET_TANK_DISPLAY_SH8601) || defined(CONFIG_POCKET_TANK_DISPLAY_ILI9341) || defined(CONFIG_POCKET_TANK_DISPLAY_ST7789)
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
#if CONFIG_POCKET_TANK_SLEEP_SCREEN || CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
static void enter_dark(int wake_after_s, bool by_face);
#endif
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
#define SLEEP_GRACE_CLOCKLESS_US (60LL * 60 * 1000000)   /* the round board (no RTC chip, 2026-10-03): its power-off wakes
                                                          through a few seconds of Wi-Fi for the time, so the in-place
                                                          wake lasts an hour (Strato: 20 min "feels cumbersome" there;
                                                          the boards with a clock chip wake seamlessly and keep 20) */
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
/* THE CLOCKLESS NIGHT (the round 1.75C, 2026-10-03). Its deep sleep draws
 * ~8 mA (an 8 h night costs ~14% of its 500 mAh), and nothing the firmware
 * can switch off explains it (docs/board-amoled-1.75c.md); the PMIC power-off
 * draws next to nothing but stops the board's only clock - and frozen time is
 * no option (Strato: the tanks must not drift apart). So with a saved Wi-Fi
 * network the night is the power-off, and the boot after it asks the internet
 * for the time (net_time_sync, a few seconds on a dark glass before the tank
 * exists). Kept in NVS:
 *   "netclk" = 1 once a sync has put the clock on real time. The FIRST sync
 *     only re-bases the stand-in clock (seeded from the build time or a save):
 *     nothing is lived for that jump. After it, every gap between a save's
 *     stamp and the internet's now is real time away.
 *   "netok"  = the last try worked. Only then is the night a power-off (the
 *     network was there this morning); otherwise a deep sleep with the clock
 *     running, as before, and every wake tries again.
 * A failed sync after a power-off resumes the clock at the save (rtc_port_seed):
 * the stretch is owed, not lost - the clock now lags real time by exactly
 * that much, and the next good sync lives it through. */
static int nvs_u8(const char *key, int dflt) {
    nvs_handle_t h; uint8_t v = (uint8_t)dflt;
    if (nvs_open("tank", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, key, &v); nvs_close(h); }
    return v;
}
static void nvs_u8_put(const char *key, int v) {
    nvs_handle_t h;
    if (nvs_open("tank", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, key, (uint8_t)v); nvs_commit(h); nvs_close(h); }
}
static bool net_saved(void) { char s[NET_SSID_MAX + 1], p[NET_PASS_MAX + 1]; return net_port_creds_get(s, p); }
static bool night_powers_off(void) {        /* the night: a power-off (the clock survives it, or the internet gives it back) */
    if (!s_pmic) return false;
    return s_rtc || (net_saved() && nvs_u8("netclk", 0) && nvs_u8("netok", 0));
}
static int64_t s_rebase_unix, s_rebase_us;  /* a first sync, applied once the save has loaded on the old clock */
static float s_boot_lived_h;
static char s_boot_sync[96] = "no sync at this boot";   /* what this boot's sync did, for `clock` (the USB log of a boot off the cable is gone) */
/* The sync runs in a worker; the glass is drawn by the boot task alone. The
 * panel driver is one task's at a time (esp_lcd's SPI io keeps a plain
 * in-flight count and takes the bus per call): 0.3.2 drew this page from its
 * own task and let the boot go on after 0.5 s whether the page had stopped
 * or not - a frame still on the wire met the tank's first frame, both waited
 * on the bus for good, and the pendant froze on CHECKING THE TIME (three
 * times, found halted 2026-10-06: syncscr and tank both in spi_device_acquire_bus). */
static volatile bool s_sync_finished;
static bool s_sync_ok;
static int64_t s_sync_u, s_sync_at;
static TaskHandle_t s_sync_waiter;
static void sync_worker(void *arg) {
    (void)arg;
    s_sync_ok = net_time_sync(&s_sync_u, &s_sync_at);
    s_sync_finished = true;
    xTaskNotifyGive(s_sync_waiter);
    vTaskDelete(NULL);
}
static void net_clock_boot(uint16_t *fb) {  /* before the tank exists: the radio has the internal heap to itself */
    if (s_rtc || !s_pmic || !net_saved()) return;
    bool real = nvs_u8("netclk", 0), set = clock_port_now_unix() != 0, ok_before = nvs_u8("netok", 0);
    if (real && set && ok_before) return;    /* a deep-sleep wake on a good clock: it ran all night */
    int64_t u = 0, at = 0;
    bool ok;
    s_sync_finished = false; s_sync_waiter = xTaskGetCurrentTaskHandle();
    if (fb && xTaskCreatePinnedToCore(sync_worker, "timesync", 8192, NULL, 5, NULL, 1) == pdPASS) {
        int64_t t0 = esp_timer_get_time();
        brightness_apply(false);
        do {                                 /* the page until the worker is done - no time limit: the tank never shares the panel */
            render_clock_sync(fb, TANK_W, (esp_timer_get_time() - t0) / 1e6f);
            display_port_flush(fb);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(40));
        } while (!s_sync_finished);
        u = s_sync_u; at = s_sync_at; ok = s_sync_ok;
    } else ok = net_time_sync(&u, &at);      /* no glass (or no task): ask in place */
    nvs_u8_put("netok", ok);
    snprintf(s_boot_sync, sizeof s_boot_sync, "%s", ok ? "got the time" : "FAILED: no network or no answer");
    if (!ok) {
        ESP_LOGW(TAG, "clock: no time from the internet - %s; tonight is a deep sleep, the next wake asks again",
                 set ? "the clock runs on as it was" : "it resumes at the save, the stretch is owed");
        return;
    }
    int64_t now = u + (esp_timer_get_time() - at) / 1000000;
    if (real) {
        int64_t was = clock_port_now_unix();
        struct timeval tv = { .tv_sec = (time_t)now }; settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "clock: set from the internet (%s) - the time away is lived through at this boot",
                 was ? "it had run on" : "it had stopped with the power");
        if (was) ESP_LOGI(TAG, "clock: it was %lld s %s", (long long)(now > was ? now - was : was - now), now >= was ? "behind (owed time, lived now)" : "ahead");
        snprintf(s_boot_sync, sizeof s_boot_sync, "got the time: %s", was ? "the clock had run on (corrected)" : "the clock had stopped (the time away lived from the save)");
    } else {
        s_rebase_unix = u; s_rebase_us = at;
        ESP_LOGI(TAG, "clock: first time from the internet - the clock moves to it once the tank is loaded, with nothing lived for the jump");
        snprintf(s_boot_sync, sizeof s_boot_sync, "got the time: the FIRST sync (clock moved, nothing lived)");
    }
}
static void net_clock_rebase(void) {        /* after progression_boot / _wake: a first sync takes over the clock */
    if (!s_rebase_us) return;
    struct timeval tv = { .tv_sec = (time_t)(s_rebase_unix + (esp_timer_get_time() - s_rebase_us) / 1000000) };
    settimeofday(&tv, NULL);
    nvs_u8_put("netclk", 1);
    s_rebase_us = 0;
    ESP_LOGI(TAG, "clock: on real time from now on - nights can be a power-off");
}
void device_clock_log(void) {               /* director `clock` */
    ESP_LOGI(TAG, "clock: this boot %s | lived at boot %.2f h | now %lld | RTC chip %s | network %s | real time %s | last sync %s | tonight: %s",
             s_boot_sync, s_boot_lived_h, (long long)clock_port_now_unix(), s_rtc ? "yes" : "no", net_saved() ? "saved" : "none",
             nvs_u8("netclk", 0) ? "yes" : "not yet", nvs_u8("netok", 0) ? "ok" : "failed or never",
             night_powers_off() ? "power-off" : "deep sleep, the clock running");
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
    if (!by_face && imu_port_face_down_now() == 1) by_face = true;
#endif
#ifdef SLEEP_SETTING_ROW
    /* (this fork) a 320x240 board: what the SLEEP row says - NEVER is the none mode's
       line, SCREEN and LIGHT the dark (which of the two, it asks itself) */
    if (sleep_setting() == SET_SLEEP_NEVER) {
        ESP_LOGI(TAG, "sleep (%s) ignored: the SLEEP row says NEVER, so the tank never goes dark", s_sleep_why);
        return;
    }
    enter_dark(wake_after_s, by_face);         /* never the stages below: no deep sleep, no power-off */
    return;
#elif CONFIG_POCKET_TANK_SLEEP_NONE
    ESP_LOGI(TAG, "sleep (%s) ignored: the sleep mode is none, so the tank never goes dark", s_sleep_why);
    return;
#elif CONFIG_POCKET_TANK_SLEEP_SCREEN || CONFIG_POCKET_TANK_SLEEP_LIGHT
    enter_dark(wake_after_s, by_face);         /* never the stages below: no deep sleep, no power-off */
    return;
#endif
    int pct0 = battery_pct(), mv0 = battery_port_vbat_mv();
    /* (this fork, 2026-10-08) the hour is for a board whose night is the
       clockless power-off: a PMIC and no clock chip. A board with no PMIC -
       a CYD or a Touch-LCD-2 built for deepsleep - has no power-off to
       sync after; its night is a deep sleep with the chip's clock running,
       so it keeps the 20 minutes the boards with a clock chip keep. Tested
       on the RTC alone, it light-slept an hour at the grace's ~4.7 mA. */
    int64_t grace_us;
    if (wake_after_s > 0) grace_us = DIRECTOR_GRACE_US;
    else if (s_rtc || !s_pmic) grace_us = SLEEP_GRACE_US;
    else grace_us = SLEEP_GRACE_CLOCKLESS_US;
    ESP_LOGI(TAG, "sleep: save, panel off, %d s grace then %s | battery %d%% %d mV",
             (int)(grace_us / 1000000),
             wake_after_s > 0 ? "deep sleep with the timer" : night_powers_off() ? (s_rtc ? "PMIC power-off (the PWR key boots it)" : "PMIC power-off (the PWR key boots it; the internet gives the time back)")
             : pwr_sensed() ? "deep sleep (the PWR key wakes; the clock runs on)" : "deep sleep (BOOT wakes)", pct0, mv0);
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
    while (!gpio_get_level(BTN_SLEEP) || pwr_sense_down()) vTaskDelay(pdMS_TO_TICKS(10));   /* wake triggers are levels: never arm them held */
    vTaskDelay(pdMS_TO_TICKS(30));
    while (battery_port_key_poll()) { }         /* the press that asked for this sleep is not the one that ends it */
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
        if (pwr_sensed()) gpio_wakeup_enable(PWR_SENSE, GPIO_INTR_HIGH_LEVEL);   /* the PWR key itself: no second of polling to wait out */
        esp_sleep_enable_gpio_wakeup();
        esp_sleep_enable_timer_wakeup(left < KEY_POLL_US ? left : KEY_POLL_US);
        esp_light_sleep_start();
        esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
        gpio_wakeup_disable(BTN_SLEEP);
        if (pwr_sensed()) gpio_wakeup_disable(PWR_SENSE);
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
        while (!gpio_get_level(BTN_SLEEP) || pwr_sense_down()) vTaskDelay(pdMS_TO_TICKS(10));   /* the wake press, still down */
        if (pwr_sensed()) { vTaskDelay(pdMS_TO_TICKS(80)); while (battery_port_key_poll()) { } }   /* ... and the PMIC's own note of it: not a new sleep press */
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
    if (wake_after_s <= 0 && night_powers_off()) {
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
/* what the round board's night turns off besides the ESP32 (2026-10-02: the
 * first night lost 21% in 12 h, ~15x the 1.8's power-off). Each saving is a
 * bit so a night (or a `deepsleep N` window) can measure one against the
 * other: director `sleepcfg <mask>`, kept in NVS. The batlog's deep row
 * carries the mask ("deep 7"). */
enum { SLEEP_TOUCH = 1, SLEEP_PANEL = 2, SLEEP_IMU = 4, SLEEP_BUS = 8, SLEEP_ALL = 15 };
static int sleep_cfg(void) {
    nvs_handle_t h; uint8_t v = SLEEP_ALL;
    if (nvs_open("tank", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "sleepcfg", &v); nvs_close(h); }
    return v & SLEEP_ALL;
}
int device_sleep_cfg(int mask) {
    if (mask >= 0) {
        nvs_handle_t h;
        if (nvs_open("tank", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "sleepcfg", (uint8_t)(mask & SLEEP_ALL)); nvs_commit(h); nvs_close(h); }
    }
    return sleep_cfg();
}
static void deep_sleep_now(int wake_after_s) {
    if (pwr_sensed()) {                         /* the PWR key's sense line (driven both ways by the board: no pull) wakes it,
                                                   as ext1 - the RTC peripherals can power down, which ext0 would keep up */
        int cfg = sleep_cfg();
        bool tp_slept = (cfg & SLEEP_TOUCH) && touch_port_deep_sleep();
        if (cfg & SLEEP_PANEL) display_port_deep_standby();
        if (cfg & SLEEP_IMU) imu_port_power_down();
        if (cfg & SLEEP_BUS) display_port_deep_sleep_bus();   /* after the deep standby: that command needs the bus */
        char why[8]; snprintf(why, sizeof why, "deep %d", tp_slept ? cfg : cfg & ~SLEEP_TOUCH);   /* what took: the log is gone with the USB port */
        batlog_add(battery_pct(), battery_port_vbat_mv(), 0, true, why);   /* mirrored to NVS: the wake reads the night's cost back */
        ESP_LOGI(TAG, "deep sleep (the PWR key wakes%s) | touch %s, panel %s, IMU %s (sleepcfg %d)", wake_after_s > 0 ? ", or the timer" : "",
                 tp_slept ? "asleep" : "in reset", cfg & SLEEP_PANEL ? "deep standby" : "sleep-in",
                 cfg & SLEEP_IMU ? "powered down" : "sensors off", cfg);
        if (cfg & SLEEP_BUS) ESP_LOGI(TAG, "QSPI clock + data held low");
        rtc_gpio_pullup_dis(PWR_SENSE); rtc_gpio_pulldown_dis(PWR_SENSE);
        esp_sleep_enable_ext1_wakeup(1ULL << PWR_SENSE, ESP_EXT1_WAKEUP_ANY_HIGH);
        display_port_deep_sleep_pins(tp_slept);
    } else {
        ESP_LOGI(TAG, "deep sleep (BOOT wakes%s)", wake_after_s > 0 ? ", or the timer" : "");
        rtc_gpio_pullup_en(BTN_SLEEP); rtc_gpio_pulldown_dis(BTN_SLEEP);
        esp_sleep_enable_ext0_wakeup(BTN_SLEEP, 0);
    }
    if (wake_after_s > 0) esp_sleep_enable_timer_wakeup((int64_t)wake_after_s * 1000000);
    audio_port_deep_sleep_pins();
    gpio_deep_sleep_hold_en();
    ESP_LOGI(TAG, "digital pads held + isolated");
    esp_deep_sleep_start();
}

#if CONFIG_POCKET_TANK_SLEEP_SCREEN || CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
/* The dark (2026-10-08): the screen and lightsleep modes' sleep, with no
 * deep sleep after it, on the 320x240 boards. The CYD in its case cannot
 * reach BOOT, and deep sleep hears nothing else, so once the grace ran out
 * the tank stayed dark for good; the Touch-LCD-2 has it too (this fork,
 * 2026-10-08), so a touch wakes it rather than BOOT alone. Here the tank
 * saves, the backlight and the sound go off, and the tank task stops
 * drawing and waits in this loop, RAM alive, for as long as it takes. It
 * lights again, where it was, on:
 *  - a touch: a finger on the glass, once the glass has been seen clear (a
 *    finger resting there as it went dark is not one).
 *  - motion: picked up or tilted off the pose it came to rest in
 *    (imu_port_rest_moved). The IMU stays awake for it.
 *  - in a dark begun face down, neither: lying on its glass, the table could
 *    be the touch, and a knock that read as motion would light it face down
 *    where the gesture, already spent, could not darken it again. Face up
 *    is its wake - turned over or picked up - as it was in the grace, but
 *    on two reads in a row, where the grace took one.
 *  - BOOT, the PMIC's PWR key, or the director's timer (`deepsleep N`).
 * It looks every DARK_SLICE_US. In lightsleep (the SLEEP row's LIGHT) the
 * chip light-sleeps between looks, woken early by BOOT or the
 * touch controller's INT line; in screen (SCREEN) - and in lightsleep while
 * a USB host is attached, since light sleep takes the USB port down and the
 * bench would lose the log - it waits awake. The
 * dark is lived through at the wake as a deep sleep is: progression_slept,
 * growth at a quarter and the full-night badge. Nothing ticks in it and the
 * tank task clamps its next frame's dt, so it is counted once. */
#define DARK_SLICE_US   100000LL    /* a look at the glass and BOOT: ~0.1 s to wake on a touch held that long */
#define DARK_SLOW_EVERY 2           /* the IMU and the PWR key every second look: ~5 a second (awake the IMU is read 4) */
#define DARK_FACE_UP_READS 2        /* a dark begun face down: not face down on this many IMU reads in a row, 0.2 s apart */

#if CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
/* (this fork) Only a 320x240 board gets here: SLEEP_LIGHT depends on
   POCKET_TANK_320X240 in Kconfig and SLEEP_SETTING_ROW is defined on those
   boards alone, so the pins held below are always one of their lists in
   board_pins.h (BOARD_DARK_HELD_PINS, 2026-10-08). */
#ifndef BOARD_DARK_HELD_PINS
#error "a board with the dark names the outputs it holds through light sleep: BOARD_DARK_HELD_PINS in board_pins.h"
#endif
/* whether the dark light-sleeps between its looks: always in a lightsleep
   build; (this fork) on a 320x240 board, when the SLEEP row says LIGHT -
   SCREEN waits awake, as the screen build does */
static bool dark_light_sleeps(void) {
#ifdef SLEEP_SETTING_ROW
    return sleep_setting() == SET_SLEEP_LIGHT;
#else
    return true;
#endif
}
/* Light sleep isolates every pad not armed as a wake (ESP-IDF's
 * ESP_SLEEP_GPIO_RESET_WORKAROUND, on for the S3): input, output and pulls
 * off, so for each slice an output is left to whatever resistor the board
 * puts on it. The outputs the dark relies on must keep their level: each
 * board names its own, and says why, in board_pins.h (BOARD_DARK_HELD_PINS:
 * on the CYD the touch reset, the amplifier's enable and the backlight; on
 * the Touch-LCD-2 the backlight and the TF card's select).
 * gpio_sleep_sel_dis keeps a pad's awake configuration through light sleep;
 * gpio_sleep_sel_en at the wake hands it back to the isolation. The other
 * pads were isolated in every 1 s slice of the old grace, and the panel, the
 * I2C bus and the IMU answered after it on the bench (CYD.md). */
static const gpio_num_t DARK_HELD_PINS[] = { BOARD_DARK_HELD_PINS };
static void dark_hold_pins(bool hold) {
    for (size_t i = 0; i < sizeof DARK_HELD_PINS / sizeof DARK_HELD_PINS[0]; i++) {
        if (hold) gpio_sleep_sel_dis(DARK_HELD_PINS[i]);
        else gpio_sleep_sel_en(DARK_HELD_PINS[i]);
    }
}
#endif

/* one wait between looks: light sleep (light, the dark's own answer from
   dark_light_sleeps, and no USB host), or awake */
static void dark_wait(int64_t us, int touch_int, bool light) {
#if CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
    if (light && !usb_serial_jtag_is_connected()) {
        gpio_wakeup_enable(BTN_SLEEP, GPIO_INTR_LOW_LEVEL);      /* released: the loop checks it every look */
        /* the touch INT only while it idles high: a level wake that already
           holds would end every slice at once */
        bool touch_armed = touch_int >= 0 && gpio_get_level((gpio_num_t)touch_int);
        if (touch_armed) gpio_wakeup_enable((gpio_num_t)touch_int, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_sleep_enable_timer_wakeup(us);
        esp_err_t slept = esp_light_sleep_start();
        gpio_wakeup_disable(BTN_SLEEP);
        if (touch_armed) gpio_wakeup_disable((gpio_num_t)touch_int);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);  /* sticky in ESP-IDF: see enter_sleep_for */
        if (slept == ESP_OK) return;
        /* refused (a wake already pending): wait awake this once, never spin */
    }
#else
    (void)touch_int; (void)light;
#endif
    TickType_t ticks = pdMS_TO_TICKS(us / 1000);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

static void enter_dark(int wake_after_s, bool by_face) {
    int pct0 = battery_pct(), mv0 = battery_port_vbat_mv();
    touch_port_confirm_answer(-1);              /* an open reset prompt is a NO */
    if (!progression_save(&tank))               /* if the cell dies in the dark, this save is the tank */
        ESP_LOGE(TAG, "dark: the tank save failed - going dark anyway, the last good save stands");
    bat_hist_save();                            /* the screen-on time so far */
    audio_port_sleep();                         /* amp off, codec down */
    batlog_add(pct0, mv0, display_port_brightness(), true, "sleep");
    display_port_sleep();                       /* backlight off, panel off */
    while (!gpio_get_level(BTN_SLEEP)) vTaskDelay(pdMS_TO_TICKS(10));   /* BOOT is a level wake: never arm it held */
    vTaskDelay(pdMS_TO_TICKS(30));
    imu_port_rest_begin();
    int touch_int = by_face ? -1 : touch_port_wake_gpio();
    /* whether this dark light-sleeps, asked once: the pins it holds at the
       start are the pins it lets go at the end, whatever the SLEEP row says
       by then. (this fork) On a 320x240 board a change of the row takes
       effect at the next sleep. */
#if CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
    bool light = dark_light_sleeps();
    const char *usb = light && usb_serial_jtag_is_connected() ? ", awake while a USB host is attached" : "";
#else
    bool light = false;
    const char *usb = "";
#endif
    ESP_LOGI(TAG, "dark (%s, %s%s): wakes on %s, BOOT%s | battery %d%% %d mV",
             s_sleep_why, SLEEP_MODE_NAME, usb, by_face ? "face up" : "a touch, motion",
             wake_after_s > 0 ? ", the timer" : "", pct0, mv0);
#if CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
    if (light) dark_hold_pins(true);            /* the board's held outputs (board_pins.h) keep their level in each slice */
#endif
    int64_t t0 = esp_timer_get_time();
    int64_t until = wake_after_s > 0 ? t0 + (int64_t)wake_after_s * 1000000 : 0;
    bool glass_clear = false;                   /* a finger counts only after the glass was seen without one */
    int face_up = 0;                            /* IMU reads in a row not face down (a dark begun face down) */
    const char *why = NULL;
    int moved = 0;
    for (int look = 1; ; look++) {
        int64_t now = esp_timer_get_time();
        if (until && now >= until) { why = "the timer"; break; }
        dark_wait(until && until - now < DARK_SLICE_US ? until - now : DARK_SLICE_US, touch_int, light);
        if (!gpio_get_level(BTN_SLEEP)) { why = "BOOT"; break; }
        if (!by_face) {
            if (!touch_port_finger_now()) glass_clear = true;
            else if (glass_clear) { why = "a touch"; break; }
        }
        if (look % DARK_SLOW_EVERY != 0) continue;
        if (battery_port_key_poll()) { why = "the PWR key"; break; }
        /* no answer from the IMU keeps it dark: a bus hiccup must not wake it */
        if (by_face) {
            /* face up on two reads in a row, 0.2 s apart. One read out of the
               face-down band is a knock on the table or a cup set down beside
               it, and a tank lit by that would stay lit lying on its glass:
               the gesture, already spent, cannot darken it again. A face-down
               read or no answer starts the count again. */
            if (imu_port_face_down_now() == 0) face_up++;
            else face_up = 0;
            if (face_up >= DARK_FACE_UP_READS) { why = "face up"; break; }
        } else {
            moved = imu_port_rest_moved();
            if (moved) { why = "motion"; break; }
        }
    }
#if CONFIG_POCKET_TANK_SLEEP_LIGHT || SLEEP_SETTING_ROW
    if (light) dark_hold_pins(false);
#endif
    while (!gpio_get_level(BTN_SLEEP)) vTaskDelay(pdMS_TO_TICKS(10));   /* a BOOT wake press, still down */
    float dark_s = (esp_timer_get_time() - t0) / 1e6f;
    progression_slept(&tank, dark_s < PROGRESSION_SLEEP_CAP_S ? dark_s : (float)PROGRESSION_SLEEP_CAP_S);
    progression_woke(&tank);                    /* a fry that was on its way: born now */
    battery_woke(&s_bh);                        /* what the gauge lost in the dark is no screen-on drain */
    display_port_wake();
    touch_port_swallow();                       /* the waking finger does nothing in the tank */
    batlog_add(battery_pct(), battery_port_vbat_mv(), display_port_brightness(), false, "woke");
    s_btn_armed = false; s_btn_low_since = 0;   /* BOOT's release is not a new press */
    if (moved) ESP_LOGI(TAG, "lit by motion (%d off the rest pose) after %.0f s dark", moved, dark_s);
    else ESP_LOGI(TAG, "lit by %s after %.0f s dark", why, dark_s);
}
#endif

void device_sleep(int wake_after_s) { s_sleep_why = "the director"; enter_sleep_for(wake_after_s); }   /* director `deepsleep N` */

/* the PWR key held 1.5 s: save and cut NOW, no grace (the same power-off the
 * grace ends in). No PMIC: deep sleep. */
static void enter_poweroff(void) {
#ifndef SLEEP_MODE_DEEP
    enter_sleep_for(0);   /* none (a 320x240 board: NEVER): ignored there, with its line; screen, lightsleep: the dark, as any sleep */
    return;
#endif
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
void device_poweroff(void) { s_sleep_why = "the director's poweroff"; enter_poweroff(); }   /* director `poweroff` */

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
    s_sleep_why = k == 2 ? "the PWR key, held" : "the PWR key";
    if (k == 2) enter_poweroff(); else enter_sleep();
}

/* BOOT: the RESET chord (2026-09-11) - while it is held, a finger landing on
 * the glass opens the confirm prompt; a finger already resting there doesn't
 * count. With no PMIC (so no PWR key) a short press, at RELEASE, is the sleep
 * key as it was until 2026-09-16; a chord press never is. */
#define BTN_DEBOUNCE_US 50000
static void sleep_button_poll(int64_t now) {
    if (gpio_get_level(BTN_SLEEP)) {
        if (!s_pmic && s_btn_armed && s_btn_low_since && !s_btn_used && now - s_btn_low_since >= BTN_DEBOUNCE_US) {
            s_sleep_why = "BOOT";
            enter_sleep();
        }
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
                    audio_port_play(SND_LOW_BATTERY, AUDIO_PITCH_ONE);   /* the cable is in: the battery's tone (0.3.2; the low notice's, a
                                                                            benign chime that serves any battery news) */
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
    sleep_setting_save();                       /* ... and the SLEEP row's, if the keeper chose one (this fork) */
    ESP_LOGI(TAG, "fresh tank: %s + %s, both fry", tank.fish[0].name, tank.fish[1].name);
    setup_begin(&tank);                         /* welcome, names, colours - as on a fresh install */
}

static void request_update(void);        /* below: CHECK FOR UPDATES */
static void tank_task(void *arg);
static void start_tank_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "starting the tank task: internal heap %u KB free (largest block %u KB)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    if (xTaskCreatePinnedToCore(tank_task, "tank", 12288, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "THE TANK TASK COULD NOT START: no internal RAM for its stack");
}
#define FRAME_MIN_MS 40                      /* the frame rate's ceiling: 25 fps (see the loop's foot) */
static void tank_task(void *arg) {
    (void)arg;
    int64_t last = esp_timer_get_time(); int cur = 0;
    int64_t last_log = last;
    int64_t render_us = 0, flush_us = 0, card_us = 0; uint32_t frames = 0, card_frames = 0;
    /* the rest of the frame (2026-10-03: render + flush were ~36 ms of a ~50 ms frame, the other ~14 untimed):
       the polls (keys, IMU, touch, the console), the tank's tick, the wait for the scene prefetch, what
       follows the flush (the next prefetch, this log), and the frame's sleep */
    int64_t polls_us = 0, tick_us = 0, wait_us = 0, tail_us = 0, sleep_us = 0, slept_from = 0;
    ESP_LOGI(TAG, "tank task up | heap int %u KB", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    for (;;) {
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f; last = now; if (dt > 0.25f) dt = 0.25f;
        if (slept_from) sleep_us += now - slept_from;
        sleep_button_poll(now);
        pwr_key_poll(now);
        imu_port_poll(now);
        if (imu_port_moving()) audio_port_prewarm();   /* in a hand: the codec stays warm (docs/AUDIO.md) */
        if (imu_port_handled()) tank_handled(&tank);   /* ... and the light stays on (two polls of motion: a bump on the desk is not a pick-up) */
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
        /* screen down and still for 2 s: the sleep key. What it does is the
           sleep mode's - on a 320x240 board the SLEEP row's, where NEVER ignores it
           with its line, as it does every other way into sleep. */
        if (imu_port_take_face_down()) {
#ifdef SLEEP_MODE_DEEP
            ESP_LOGI(TAG, "face down: sleeping (face up wakes it within the grace)");
#endif
            s_sleep_why = "face down";
            s_sleep_by_face = true;
            enter_sleep();
        }
        /* with an IMU answering, the IMU turns the picture and the settings
           row is ROTATION, not SCREEN: a saved SCREEN choice is set aside */
        bool live = s_imu ? imu_port_inverted() : orientation_flipped();
#elif CONFIG_POCKET_TANK_IMU_AUTO_FLIP
        bool live = imu_port_inverted() != orientation_flipped();   /* the IMU's flip, turned again by the keeper's SCREEN choice */
#else
        bool live = orientation_flipped();   /* no auto-flip: the keeper's SCREEN choice alone (the IMU still senses handling) */
#endif
        bool inv = tank_orient(&tank, live);   /* the live flip, or the way up settings' ROTATION locked (0.3.2) */
#if CONFIG_POCKET_TANK_320X240
        /* (this fork) a 320x240 board offers ROTATION only with an IMU
           answering in a face-down build (render_settings_set_imu, below) -
           the CYD with its MPU-6050; otherwise its row is SCREEN (the
           Touch-LCD-2, whose IMU does not run the gesture).
           A lock saved while an IMU answered stays in the save but does not
           hold the picture then: SCREEN could not turn it, and no row could
           unlock it. */
        bool rotation_row = false;
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
        rotation_row = s_imu;
#endif
        if (!rotation_row) inv = live;
#endif
#ifdef TANK_WATCH
        inv = tank_screen_turned(&tank);  /* worn on a wrist the live flip never runs (the arm swings through every angle):
                                             the way up is the keeper's setting, or what AUTO learned from the taps (tank.h) */
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
          else if (w == SET_TAP_FEED) ESP_LOGI(TAG, "settings: auto feed %s", v ? "ON" : "OFF (the keeper feeds; a starving fish loses trust)");
          else if (w == SET_TAP_ROTATE) ESP_LOGI(TAG, "settings: rotation %s", v ? (tank.orient_inv ? "LOCKED (turned over)" : "LOCKED (upright)") : "unlocked (the picture follows the tank)");
          else if (w == SET_TAP_FLIP) orientation_set(v != 0);              /* the CYD's SCREEN row (no IMU) */
          else if (w == SET_TAP_SLEEP) sleep_setting_set(v); }              /* the 320x240 boards' SLEEP row: NEVER / SCREEN / LIGHT */
        if (touch_port_take_update() == UPD_TAP_CHECK) request_update();   /* the updates page's CHECK: save, restart into update mode */
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
        int64_t pf_tick = esp_timer_get_time(); polls_us += pf_tick - now;
        tank.hold_light = setup_active() || touch_port_confirm_up();   /* no lights-out mid-name */
        tank.ui_cover = tank.hold_light || touch_port_milestones() || touch_port_settings() || touch_port_updates() || touch_port_shop() || touch_port_battery();   /* a fry's spawning waits */
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
        int64_t pf_wait = esp_timer_get_time(); tick_us += pf_wait - pf_tick;
        int64_t pf_tail = pf_wait;
        if (fb[cur]) {
            if (s_prefetch_pending) {                       /* prior frame's scene prefetch */
                xSemaphoreTake(s_amc_done, portMAX_DELAY);
                s_prefetch_pending = false;
                render_fb_primed(s_prefetch_fb, s_prefetch_ep);
            }
            int64_t t0 = esp_timer_get_time();
            wait_us += t0 - pf_wait;
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
#if CONFIG_POCKET_TANK_320X240
                /* (this fork) a 320x240 board's row is chosen at run time (common/render.h):
                   SCREEN with no IMU, ROTATION with one in a face-down build; SLEEP under
                   it, but not on a deepsleep build */
                render_settings_set_flip(orientation_flipped());
#if CONFIG_POCKET_TANK_IMU_FACE_DOWN_SLEEP
                render_settings_set_imu(s_imu);
#endif
                render_settings_set_sleep(sleep_setting());   /* -1 on a deepsleep build: no row */
#endif
                render_settings(&tank, fb[cur], TANK_W, brightness_level(), audio_port_volume());
                sel = -1;
            } else if (touch_port_updates()) {   /* the updates page (2026-09-30): version, network, CHECK FOR UPDATES */
                render_updates_page(fb[cur], TANK_W);
                sel = -1;
            } else if (touch_port_shop()) {      /* the shop: sand dollars and what they buy */
                render_shop(&tank, fb[cur], TANK_W);
                sel = -1;
            } else render_sd_toast(&tank, fb[cur], TANK_W);   /* the live tank: "+N" as dollars are earned */
            if (sel >= 0) render_stats_card(&tank, sel, fb[cur], TANK_W);   /* tapped fish: stats card (+ the pill) */
            {   /* the battery pill: with a card, while low, and a few seconds after the cable goes in;
                   the battery page (a tap on the pill) over the live tank in its place */
                bool pages = touch_port_milestones() || touch_port_settings() || touch_port_updates() || touch_port_shop() || setup_active() || touch_port_confirm_up();
                bool bpage = touch_port_battery() && s_bat_ok && !pages;
                bool pill = s_bat_ok && !pages && !bpage && (sel >= 0 || s_bat_low || now < s_bat_popup_us);
                if (bpage) {
                    bat_info_t bi;
                    battery_info(&s_bh, clock_port_now_unix(), (int)(s_bat_frac * 100 + 0.5f), s_bat_mv, s_bat_state, &bi);
                    render_battery_info(fb[cur], TANK_W, &bi, tank.clock);
                } else if (pill) render_battery(fb[cur], TANK_W, s_bat_frac, s_bat_state, tank.clock);
                if (!pages && !bpage) render_tool_chip(&tank, sel, fb[cur], TANK_W);   /* a tool in hand: the chip (2026-10-01) */
                touch_port_set_pill(pill);
            }
            if (!touch_port_milestones() && !touch_port_settings() && !touch_port_updates() && !touch_port_shop()) {   /* an announcement over the live tank */
                const notice_t *nt = notice_current();
                if (nt) render_notice(&tank, fb[cur], TANK_W, nt->kind, nt->fish, nt->bit, 1.0f - nt->age / NOTICE_UP_S);
            }
            if (setup_active())                  /* first-run setup: over the tank, under the prompt */
                render_setup(&tank, fb[cur], TANK_W, tank.clock);
            if (touch_port_confirm_up())         /* reset prompt: over everything, fish still swim */
                render_confirm_reset(fb[cur], TANK_W, touch_port_confirm_frac());
            int64_t t1 = esp_timer_get_time();
            if (sel >= 0) { card_us += t1 - tc; card_frames++; }
            /* the next frame's scene prefetch starts BEFORE this frame's flush (2026-10-03): the copy
               into the idle framebuffer takes ~14 ms, and started after the flush it had only the
               tick and a 1 ms sleep to hide behind - the next frame waited ~9 ms for it. The flush
               only reads this frame's buffer; the copy rides alongside it. */
            { uint16_t *next = fb[cur ^ 1];
              const uint16_t *scene = render_scene_buf(&s_prefetch_ep);
              if (s_amc && scene && next && next != fb[cur] &&
                  esp_async_memcpy(s_amc, next, (void *)scene, PLAN_FB_ALLOC, amc_cb, NULL) == ESP_OK) {
                  s_prefetch_fb = next; s_prefetch_pending = true;
              } }
            display_port_flush(fb[cur]);
            touch_port_poll(&tank);
            pf_tail = esp_timer_get_time();
            render_us += t1 - t0; flush_us += pf_tail - t1; frames++;
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
                int64_t fw, fs; display_port_flush_prof(&fw, &fs);
                ESP_LOGI("display", "frame %.1f ms = polls %.1f + tick %.1f + prefetch wait %.1f + render %.1f + flush %.1f + tail %.1f + sleep %.1f | flush: fill %.1f, wire wait %.1f, send calls %.1f",
                         (float)(now - last_log) / 1e3f / frames, polls_us / 1e3f / frames, tick_us / 1e3f / frames, wait_us / 1e3f / frames,
                         render_us / 1e3f / frames, flush_us / 1e3f / frames, tail_us / 1e3f / frames, sleep_us / 1e3f / frames,
                         (flush_us - fw - fs) / 1e3f / frames, fw / 1e3f / frames, fs / 1e3f / frames);
                card_us = 0; card_frames = 0;
                memset(render_prof_us, 0, sizeof render_prof_us);
            }
            render_us = flush_us = 0; frames = 0;
            polls_us = tick_us = wait_us = tail_us = sleep_us = 0;
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
        /* the frame's ceiling (2026-10-04): FRAME_MIN_MS a frame, 25 fps. Frames and decisions share one
           budget - measured on the 1.8, the same code at 16.6 fps decides in 3.69 s and at 22.8 fps in
           3.93 s (~40 ms a decision per frame a second; the bigger glasses pay more). Strato: decisions
           past 4 s are too slow, so a light tank no longer spends the model's time on frames past 25.
           Always yield >= 1 tick. */
        int rest = (int)((FRAME_MIN_MS * 1000 - (esp_timer_get_time() - now)) / 1000);
        slept_from = esp_timer_get_time(); tail_us += slept_from - pf_tail;
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

/* CHECK FOR UPDATES (the updates page, or the director's `ota check`): the
 * tank is saved as it is before sleep, then the board restarts into update
 * mode (update_mode.c) - the radio never runs beside the tank */
static void request_update(void) {
#if CONFIG_POCKET_TANK_320X240
    /* (this fork) the 320x240 boards have no update channel (no manifest of
       their own, flashed by cable: CYD.md): no restart into update mode,
       which app_main would not enter anyway */
    ESP_LOGW(TAG, "update check: a 320x240 board is updated by cable (CYD.md), not over the air");
    return;
#endif
    touch_port_confirm_answer(-1);
    progression_save(&tank);
    bat_hist_save();
    batlog_add(battery_pct(), battery_port_vbat_mv(), display_port_brightness(), false, "update");
    audio_port_sleep();
    update_mode_request();                   /* restarts */
}
void device_update_check(void) { request_update(); }
/* the installer page asked a RUNNING tank for the networks (its Connect to /
 * Change Wi-Fi): saved the same way, then the restart into provisioning mode */
void device_provision_request(void) {
#if CONFIG_POCKET_TANK_320X240
    ESP_LOGW(TAG, "provisioning: a 320x240 board has no update channel, so no Wi-Fi to set up");
    return;
#endif
    touch_port_confirm_answer(-1);
    progression_save(&tank);
    bat_hist_save();
    batlog_add(battery_pct(), battery_port_vbat_mv(), display_port_brightness(), false, "wifi");
    audio_port_sleep();
    provision_mode_request();                /* restarts */
}

void app_main(void) {
    ESP_LOGI(TAG, "pocket-tank v%s %s (build %s) for %s boot%s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string(), PT_BOARD,
             esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 ? " (woken by button)" : "");
    gpio_config_t btn = { .pin_bit_mask = 1ULL << BTN_SLEEP, .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&btn);
    gpio_deep_sleep_hold_dis();              /* a deep-sleep wake is a boot: the night's pad holds end here */
    nvs_start();
    director_early();                        /* the installer page's handshake, answered from here on (director.c) */
    { int carried = batlog_init();       /* the battery log survives every reset but a power-on */
      if (carried) ESP_LOGI(TAG, "batlog: %d samples carried through the reset (director `batlog` reads them)", carried); }
    brightness_init();
    orientation_init();
    sleep_setting_init();                    /* (this fork) the 320x240 boards' SLEEP row */
#ifdef SLEEP_SETTING_ROW
    ESP_LOGI(TAG, "sleep mode: %s (%s)", SLEEP_MODE_NAME, sleep_setting_saved() ? "chosen in settings" : "the build's default, nothing chosen in settings");
#endif
    bat_hist_load();
    assert_plan();
    for (int i = 0; i < PLAN_FB_COUNT; i++) {
        fb[i] = heap_caps_aligned_alloc(64, PLAN_FB_ALLOC, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!fb[i]) fb[i] = i ? fb[0] : NULL;          /* no PSRAM: share or skip */
    }
    if (!fb[0]) ESP_LOGW(TAG, "no framebuffer RAM: rendering disabled (tank still runs)");
    /* the board first: the display (it owns the I2C bus), the glass, the
       PMIC - then, if this boot was asked for, UPDATE MODE before anything
       else exists (docs/OTA.md): the pages over the radio with the whole
       internal heap to itself. Back from it, the normal boot goes on. */
    render_clock_us = esp_timer_get_time;    /* per-stage frame profiling in the display log */
    display_port_init();
    if (pwr_sensed()) {                      /* the PWR key's sense line: a plain input (a deep-sleep wake left it an RTC pad) */
        rtc_gpio_deinit(PWR_SENSE);
        gpio_config_t sense = { .pin_bit_mask = 1ULL << PWR_SENSE, .mode = GPIO_MODE_INPUT };
        gpio_config(&sense);
    }
    touch_port_init();
    s_pmic = battery_port_init(board_i2c_bus());
    if (!board_has_expander()) {      /* not positively the 1.8: on the round board A3V3 feeds the ES7210 whole,
                                         and off, it clamps the I2C bus (battery_port.h) - never cut it on a guess */
        battery_port_pin_rail("aldo1");
        codec_port_mic_adc_down(board_i2c_bus());
    }
    if (board_is_watch()) battery_port_pin_rail("aldo2");   /* the watch's panel power enable is pulled up to ALDO2 */
    battery_port_trim_rails();        /* the schematic's unused outputs off (docs/HANDOFF.md, the battery pass) */
    battery_port_key_init();          /* the PWR key: sleep / power-off IRQs on, the power-on press cleared */
#if CONFIG_POCKET_TANK_320X240
    /* (this fork) a 320x240 board: no update mode and no provisioning wait (it
       would hold the glass dark 8 s after every reset over USB with no network
       saved). The request word is still read, so a stale one is cleared. */
    (void)update_mode_pending();
#else
    if (update_mode_pending()) { brightness_apply(false); update_mode_run(fb[0]); }
    else if (provision_mode_wanted()) provision_mode_run(fb[0]);   /* just installed, no network yet: the page's Wi-Fi step, the glass dark */
#endif
    s_rtc = rtc_port_init(board_i2c_bus());   /* wall clock for the ravenous rule (before the clockless night's sync) */
    net_clock_boot(fb[0]);            /* no RTC chip and a saved network: the time from the internet, before the advisor takes the heap */
    /* the advisor's hot buffers (~70 KB of internal SRAM, advisor_llm_esp.c)
       come before the discretionary caches below: the LLM is not optional */
    /* model: mmap the raw partition; weights are read through the flash cache */
    const esp_partition_t *mp = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "model");
    if (!mp) { ESP_LOGE(TAG, "no model partition"); }
    else {
        const void *map; esp_partition_mmap_handle_t h;
        if (esp_partition_mmap(mp, 0, mp->size, ESP_PARTITION_MMAP_DATA, &map, &h) == ESP_OK) {
            model_trailer_check(mp, map);    /* the trailer stands, or is healed for the shipped model (docs/OTA.md) */
            llm_ok = advisor_llm_esp_init(map, mp->size, tokenizer_bin_start,
                                          tokenizer_bin_end - tokenizer_bin_start);
            ESP_LOGI(TAG, "model partition %u KB mmap'd, advisor %s", (unsigned)mp->size / 1024, llm_ok ? "LLM" : "rules (model missing)");
        } else ESP_LOGE(TAG, "model mmap failed");
    }
    ESP_LOGI(TAG, "after the advisor: internal heap %u KB free", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    /* static-scene cache: gradient/pebbles/reef drawn once per lighting state */
    uint16_t *scene = heap_caps_aligned_alloc(64, PLAN_FB_ALLOC, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
    /* 2026-09-30: the Wi-Fi stack's statics (docs/OTA.md) took ~36 KB of internal
       RAM, and the card cache is the one big discretionary block: it stays
       internal only while that leaves INTERNAL_MARGIN for the tank task's stack
       and the runtime (NVS writes, the audio path); otherwise PSRAM, ~2 ms more
       per frame only while a card is up */
    #define INTERNAL_MARGIN (12288 + 40 * 1024)
    uint16_t *card = NULL;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= RENDER_CARD_W * RENDER_CARD_H * 2 + INTERNAL_MARGIN)
        card = heap_caps_malloc(RENDER_CARD_W * RENDER_CARD_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!card) card = heap_caps_malloc(RENDER_CARD_W * RENDER_CARD_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (card) render_set_card_cache(card);
    ESP_LOGI(TAG, "card cache %s", !card ? "none" : esp_ptr_external_ram(card) ? "PSRAM" : "internal SRAM");
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
    tank_init(&tank, (uint32_t)esp_timer_get_time() ^ 0xC0FFEEu);
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool from_sleep = cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER;
    if (from_sleep) {                        /* the night, lived through in one step */
        float h = progression_wake(&tank, clock_port_now_unix());
        s_boot_lived_h = h;
        ESP_LOGI(TAG, "wake from deep sleep (%s): %s%.1f h simulated | hunger[0] %.1f | battery %d%% %d mV",
                 cause == ESP_SLEEP_WAKEUP_TIMER ? "timer" : cause == ESP_SLEEP_WAKEUP_EXT1 ? "the PWR key" : "BOOT", h < 0 ? "no clock, " : "", h < 0 ? 0.0f : h,
                 tank.n_fish ? tank.fish[0].hunger : 0.0f, battery_pct(), battery_port_vbat_mv());
        batlog_add(battery_pct(), battery_port_vbat_mv(), 0, true, "wake");
        net_clock_rebase();
        int put_back = restore_fish();       /* where they fell asleep, on the goal they had */
        ESP_LOGI(TAG, "wake: %d of %d fish put back where they were", put_back, tank.n_fish);
    } else {                                 /* a cold boot - power-on, a flash, a PMIC power-off, a cell that died: the absence is lived through just the same (2026-09-16) */
        float h = progression_boot(&tank);
        s_boot_lived_h = h;
        ESP_LOGI(TAG, "cold boot: %s%.1f h lived through since the save | hunger[0] %.1f | battery %d%% %d mV",
                 h < 0 ? "no save or no clock, " : "", h < 0 ? 0.0f : h,
                 tank.n_fish ? tank.fish[0].hunger : 0.0f, battery_pct(), battery_port_vbat_mv());
        net_clock_rebase();
        if (!s_rtc && clock_port_now_unix() == 0) rtc_port_seed(progression_loaded_unix());   /* a clockless board after a power cut: time resumes at the save */
    }
    notice_sync(&tank);                      /* what is already earned stays unannounced */
    { uint32_t r = progression_loaded_release();   /* which release wrote the tank we just loaded */
      if (r) ESP_LOGI(TAG, "the save was written by v%d.%d.%d", (int)(r >> 16), (int)(r >> 8 & 255), (int)(r & 255));
      else ESP_LOGI(TAG, "the save predates release numbers (or there was none)");
      if (r && r < (uint32_t)PT_RELEASE_NUM) notice_updated();   /* the first boot of a new release: its card, once (the next save stamps this release) */ }
    /* the save loaded under this image: it is good - the bootloader's
       rollback (a fresh image boots "pending" once) stands down. Before the
       first save write, so an older build never meets a newer save it
       cannot read (it reads its prefix anyway: persist_port_esp.c) */
    { esp_ota_img_states_t st; const esp_partition_t *run = esp_ota_get_running_partition();
      if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
          esp_ota_mark_app_valid_cancel_rollback();
          ESP_LOGI(TAG, "first boot of this image from %s: marked valid, rollback cancelled", run->label); } }
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
    /* the tank task starts from a one-shot timer, AFTER app_main has returned
       and its own 16 KB stack is back in the internal heap (2026-09-30: with
       the Wi-Fi stack linked, creating it from here found 11 KB free and the
       tank never started) */
    ESP_LOGI(TAG, "app_main done: internal heap %u KB free (largest block %u KB); the tank task starts once this stack is freed",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    const esp_timer_create_args_t st = { .callback = start_tank_task, .name = "tank-start" };
    esp_timer_handle_t th; esp_timer_create(&st, &th); esp_timer_start_once(th, 200000);
}
