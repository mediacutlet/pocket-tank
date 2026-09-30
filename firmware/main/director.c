/* director.c — serial console for staging scenarios (see director.h).
 * RX only through the USB-Serial-JTAG driver; the log keeps its polled
 * (no-driver) write path so an unattended tank never blocks on a host that
 * isn't reading. Called from the tank task, so no locking. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"          /* vTaskDelay: no longer pulled in indirectly on ESP-IDF 6 */
#include "freertos/task.h"
#include "progression.h"
#include "setup.h"
#include "director.h"
#include "touch_port.h"
#include "battery_port.h"
#include "display_port.h"
#include "brightness.h"
#include "batlog.h"
#include "codec_port.h"
#include "audio_port.h"
#include "imu_port.h"
#include "audio.h"
#include "notice.h"
#include "esp_timer.h"
#include "nvs.h"
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#endif

static const char *TAG = "director";

/* the real tank's save, parked while a staged tank (fresh / stages) lives in
 * its place: NVS blob "bk" beside progression's "save" in namespace "tank".
 * The staged tank autosaves over "save" like any other; `restore` copies "bk"
 * back and reboots progression from it. */
static bool nvs_copy(const char *from, const char *to) {
    nvs_handle_t h; if (nvs_open("tank", NVS_READWRITE, &h) != ESP_OK) return false;
    size_t len = 0; bool ok = false;
    if (nvs_get_blob(h, from, NULL, &len) == ESP_OK && len > 0) {
        void *buf = malloc(len);
        if (buf && nvs_get_blob(h, from, buf, &len) == ESP_OK &&
            nvs_set_blob(h, to, buf, len) == ESP_OK && nvs_commit(h) == ESP_OK) ok = true;
        free(buf);
    }
    nvs_close(h); return ok;
}
static bool nvs_has(const char *key) {
    nvs_handle_t h; if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0; bool ok = nvs_get_blob(h, key, NULL, &len) == ESP_OK && len > 0;
    nvs_close(h); return ok;
}
static void nvs_drop(const char *key) {
    nvs_handle_t h; esp_err_t e = nvs_open("tank", NVS_READWRITE, &h);
    if (e == ESP_OK) { e = nvs_erase_key(h, key); if (e == ESP_OK) e = nvs_commit(h); nvs_close(h); }
    if (e != ESP_OK) ESP_LOGW(TAG, "drop %s failed: %s", key, esp_err_to_name(e));
    else if (nvs_has(key)) ESP_LOGW(TAG, "drop %s: still there after the erase", key);
}
static bool stash(tank_t *t) {
    progression_save(t);
    bool ok = nvs_copy("save", "bk");
    ESP_LOGI(TAG, "%s", ok ? "real tank stashed (restore brings it back)" : "STASH FAILED - not touching the tank");
    return ok;
}
/* a staged tank replaces the live one: park the real save first unless a
 * stash is already parked (a second staging must not overwrite it) */
static bool stage_guard(tank_t *t) {
    if (nvs_has("bk")) { ESP_LOGI(TAG, "stash already parked; staging over the current tank"); return true; }
    return stash(t);
}
/* a courtship / arrival scene needs room under the cap: the newest fish steps
 * out of the STAGED tank (the parked one keeps it) */
static void make_room(tank_t *t) {
    if (t->n_fish < POP_CAP && t->n_fish < N_FISH_MAX) return;
    t->n_fish--;
    ESP_LOGI(TAG, "tank at the cap: %s steps out for the scene (the parked tank keeps it)", t->fish[t->n_fish].name);
}
static const float STAGE_AGE[4] = { 0, STAGE_JUV_AGE + 60, STAGE_ADULT_AGE + 60, STAGE_ELDER_AGE + 60 };
static int stage_of(const char *s) {
    for (int i = 0; i < 4; i++) if (!strcasecmp(s, STAGE_NAMES[i])) return i;
    if (!strcasecmp(s, "juvenile")) return STAGE_JUV;
    return -1;
}
static bool s_ok;
static char s_line[96];
static int  s_len;

static int find_fish(const tank_t *t, const char *s) {
    if (!s) return -1;
    if (isdigit((unsigned char)s[0])) { int i = atoi(s); return i >= 0 && i < t->n_fish ? i : -1; }
    for (int i = 0; i < t->n_fish; i++)
        if (!strcasecmp(t->fish[i].name, s)) return i;
    return -1;
}

static float *drive_of(fish_t *f, const char *s) {
    if (!strcmp(s, "hunger"))    return &f->hunger;
    if (!strcmp(s, "energy"))    return &f->energy;
    if (!strcmp(s, "stress"))    return &f->stress;
    if (!strcmp(s, "curiosity")) return &f->curiosity;
    if (!strcmp(s, "trust"))     return &f->trust;
    return NULL;
}

static void clear_pellets(tank_t *t) {
    for (int i = 0; i < MAX_FOOD; i++) t->food[i].alive = false;
}

static void show_state(const tank_t *t) {
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        ESP_LOGI(TAG, "%d %-6s %-5s age %.1fh size %.2f hunger %.1f energy %.1f stress %.1f curiosity %.1f trust %.1f ms %03x  %s at %.0f,%.0f",
                 i, f->name, STAGE_NAMES[f->stage], progression_age_s(t, i) / 3600.0f, f->size,
                 f->hunger, f->energy, f->stress, f->curiosity, f->trust, (unsigned)f->ms_bits,
                 GOAL_NAMES[f->goal.id], f->x, f->y);
    }
    int pellets = 0, cells = 0;
    for (int i = 0; i < MAX_FOOD; i++) pellets += t->food[i].alive;
    for (int i = 0; i < ALGAE_CELLS; i++) cells += t->algae[i] > 0;
    ESP_LOGI(TAG, "pellets %d | trickle %s | ravenous %d | %s%s idle %.0fs | veg %.2f %.2f %.2f | algae cells %d/%d | courting %s%s%s%s | arrival %s",
             pellets, t->trickle_off ? "OFF" : "on", (int)t->ravenous,
             t->night ? "night" : "day", t->light_override ? " (manual)" : "",
             t->idle_s,
             t->veg_growth[0], t->veg_growth[1], t->veg_growth[2], cells, ALGAE_CELLS,
             t->courting ? t->fish[t->court_a].name : "no", t->courting ? "+" : "",
             t->courting ? t->fish[t->court_b].name : "", t->spawning ? " (SPAWNING)" : t->court_active > 0 ? " (circling)" : "",
             progression_arrival_pending() ? "staged" : "-");
    ESP_LOGI(TAG, "sand dollars %d (earned %d) | shop:%s%s%s | colonies %d | %.0f cm trimmed",
             (int)t->sd_balance, (int)t->sd_earned, t->sd_unlocks & SD_ITEM_PLANT ? " plant" : "", t->sd_unlocks & SD_ITEM_SNAIL ? " snail" : "",
             t->sd_unlocks ? "" : " -", (int)t->algae_colonies, t->trim_px / PX_PER_CM);
    if (t->sd_unlocks & SD_ITEM_SNAIL) {                /* where it is, what it is after */
        int c = t->snail_cell, cells = 0; for (int i = 0; i < ALGAE_CELLS; i++) cells += t->algae[i] > 0;
        ESP_LOGI(TAG, "snail at %.0f,%.0f heading %.0f deg | %s cell %d at %d,%d (film %d) | %d cells on the glass | %d grazed so far", t->snail_x, t->snail_y,
                 t->snail_heading * 57.3f, c >= 0 ? "after" : "no target,", c, c >= 0 ? (c % ALGAE_COLS) * ALGAE_CELL + 8 : -1,
                 c >= 0 ? (c / ALGAE_COLS) * ALGAE_CELL + 8 : -1, c >= 0 ? t->algae[c] : 0, cells, (int)t->snail_grazed);
    }
    for (int b = 0; b < tank_veg_beds(t); b++) {   /* per-frond heights: which blades a sweep left standing */
        char row[VEG_FRONDS_MAX * 5 + 1]; int len = 0, n; float x0, f0, f1;
        tank_veg_bed(t, b, &x0, NULL, NULL, &n);
        tank_veg_frond(t, b, 0, &f0); tank_veg_frond(t, b, 1, &f1);     /* the pitch: 12, the sword plant's 14 */
        for (int i = 0; i < n && len < (int)sizeof row - 5; i++) len += snprintf(row + len, sizeof row - len, " %.2f", t->veg_h[b][i]);
        ESP_LOGI(TAG, "bed %d %s (x from %.0f, pitch %.0f):%s", b, b == 3 ? "leaves" : "fronds", x0 + 6, f1 - f0, row);
    }
    if (t->sd_unlocks & SD_ITEM_PLANT)
        ESP_LOGI(TAG, "plant placed: centre x %.0f (%s), depth %s", tank_decor_x(t, 0), t->plant_x > 0 ? "the keeper's" : "the default",
                 t->plant_z == DECOR_Z_BACK ? "BEHIND the fish" : t->plant_z == DECOR_Z_FRONT ? "IN FRONT of the fish" : "AMONG the fish");
    if (t->sd_unlocks & SD_ITEM_CASTLE)
        ESP_LOGI(TAG, "castle placed: centre x %.0f (%s), %s", tank_decor_x(t, 2), t->castle_x > 0 ? "the keeper's" : "the default",
                 t->castle_z == DECOR_Z_BACK ? "BEHIND the grass" : "IN FRONT of the grass (the fish swim through)");
    if (t->sd_unlocks & SD_ITEM_CORAL)
        ESP_LOGI(TAG, "coral placed: centre x %.0f (%s), %s, colour %06x, growth %.2f of %.2f (%s)", tank_decor_x(t, 3), t->coral_x > 0 ? "the keeper's" : "the default",
                 t->coral_z == DECOR_Z_BACK ? "BEHIND" : t->coral_z == DECOR_Z_FRONT ? "IN FRONT" : "AMONG the grass", (unsigned)tank_coral_rgb(t),
                 tank_coral_growth(t), CORAL_FULL, tank_coral_growth(t) >= CORAL_FULL ? "the crown is out" : tank_coral_growth(t) >= 1 ? "the fan is complete, the crown coming" : "growing");
    if (t->sd_unlocks & SD_ITEM_CLUSTER)
        ESP_LOGI(TAG, "reef cluster placed: centre x %.0f (%s), %s, look %s, growth %.2f of %.0f (%s)", tank_decor_x(t, 4), t->cluster_x > 0 ? "the keeper's" : "the default",
                 t->cluster_z == DECOR_Z_BACK ? "BEHIND" : t->cluster_z == DECOR_Z_FRONT ? "IN FRONT" : "AMONG the grass", CLUSTER_SCHEMES[tank_cluster_scheme(t)].name,
                 tank_cluster_growth(t), CLUSTER_FULL, tank_cluster_growth(t) >= CLUSTER_FULL ? "in full bloom" : tank_cluster_growth(t) >= 1 ? "full size, blooming" : "filling out");
    ESP_LOGI(TAG, "nursery bed %d (a bed >= %.2f) | parked real tank: %s", tank_nursery_bed(t), (double)VEG_NURSERY,
             nvs_has("bk") ? "YES (restore)" : "no (this IS the real tank)");
    float bf; bool chg;
    if (battery_port_read(&bf, &chg)) {
        int st = battery_port_state();
        ESP_LOGI(TAG, "battery %.0f%% %s, VBAT %d mV | brightness %d/255 (level %d%%)", bf * 100,
                 st == BAT_CHARGING ? "charging" : st == BAT_FULL ? "on the cable, full" : st == BAT_PLUGGED ? "on the cable, not charging" : "on battery",
                 battery_port_vbat_mv(), display_port_brightness(), brightness_level());
    }
    device_battery_log();
}

static void help(void) {
    ESP_LOGI(TAG, "help | state | hungry [N] [level=9] (N fish starving, water cleared, trickle held)");
    ESP_LOGI(TAG, "fed [level=1] (everyone full, trickle back on) | set <hunger|energy|stress|curiosity|trust> <0-10> [fish name|idx]");
    ESP_LOGI(TAG, "feed [n=3] [x] (keeper drops pellets; trickle back on) | trickle on|off | pellets clear");
    ESP_LOGI(TAG, "algae <steps|clear> | veg <bed 0-2|all> <0.03-1> | light (toggle) | auto | sleep <hours> | save");
    ESP_LOGI(TAG, "STAGED TANKS (the real one is parked first): fresh (new tank, two fry) | stages (fry juv adult elder) | stage <fish|all> <fry|juv|adult|elder>");
    ESP_LOGI(TAG, "stash (park the real tank now) | restore (bring it back) | age <fish> <hours>");
    ESP_LOGI(TAG, "milestones [off] (the page, on cue; on the device: tap the open stats card)");
    ESP_LOGI(TAG, "shop [off] (the sand dollar page) | dollars [n] (grant n; the balance and the chore counts) | buy plant|snail|castle|coral (at the price) | place [plant|castle|coral] [x [behind|among|front]] (the piece's spot; no x = the page; the castle has no among) | coral <0-7|rrggbb> (its colour) | coral grow <g> (its growth, 1 = the fan, 1.25 = the crown) | cluster look <0-2> | cluster grow <g> (1 = full size, 2 = every tentacle) | sell plant|castle|coral|cluster (20%% back)");
    ESP_LOGI(TAG, "reset (the keeper's confirm prompt, as BOOT + tap opens it) | reset yes|no (answer it here) - YES WIPES EVERY SAVE, a parked tank too");
    ESP_LOGI(TAG, "setup [off] (the first-run flow: welcome, names, colours; off drops the panel - the birth flow too) | name <fish|idx> <newname> (up to %d letters, saved)", FISH_NAME_MAX);
    ESP_LOGI(TAG, "battery <pct> [charging|full|plugged]|real (a STAGED gauge: on battery at pct - the card's pill, and at 10 or less the low-battery notice + cue + the pill that stays - or on the cable: the bolt, the sweep while charging, the pill for a few seconds; not saved) | battery page [off] (the battery page, as a tap on the pill opens it) | battery (its numbers) | snd battery (just the notice + cue)");
    ESP_LOGI(TAG, "kbd [wheel|grid|pages] (the name page's design: the wheel, or one of the two rejected keyboards of 09-13 - not saved, a boot is the wheel)");
    ESP_LOGI(TAG, "touch [bias <px>] (finger-landing correction: reported touches move up by px; not saved)");
    ESP_LOGI(TAG, "pmic (AXP2101 dump) | pmic on|off <aldo1|aldo2..4|bldo1|bldo2|cpusldo|dcdc2..5|dldo1|dldo2> (experiments; boot trims the unused ones) | pmic trim");
    ESP_LOGI(TAG, "bright <0-255> (panel now; not saved) | level 100|60|30 (the keeper's setting, saved)");
    ESP_LOGI(TAG, "batlog [clear] (the tank's own battery log: SoC/VBAT every 5 min awake, 30 min asleep, mA derived - read it after a night on battery) | codec (ES8311 registers) | deepsleep [N] (N: 5 s grace then deep sleep with an N s timer wake - one batlog window per N, BOOT wakes it; no N: the keeper's sleep, grace then power-off) | poweroff (save + PMIC cut now) | keytime [N] (N s of timing every PWR press - is a tap under the PMIC's 128 ms power-on hold?)");
    ESP_LOGI(TAG, "overgrown (grass to the ceiling + fouled glass; fish stress climbs) | court (stage the fry: in ~10-20 s the pair courts in the grass, then it is born) | arrive (the fry, now)");
}

static void run(tank_t *t, char *line) {
    char *argv[6]; int argc = 0;
    for (char *tok = strtok(line, " \t"); tok && argc < 6; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    if (!argc) return;
    for (char *p = argv[0]; *p; p++) *p = (char)tolower((unsigned char)*p);
    const char *c = argv[0];
    if (!strcmp(c, "help")) help();
    else if (!strcmp(c, "state")) show_state(t);
    else if (!strcmp(c, "hungry")) {
        int n = argc > 1 ? atoi(argv[1]) : t->n_fish;
        float lvl = argc > 2 ? atof(argv[2]) : 9.0f;
        if (n > t->n_fish) n = t->n_fish;
        for (int i = 0; i < n; i++) t->fish[i].hunger = lvl;
        clear_pellets(t);
        t->trickle_off = true;
        ESP_LOGI(TAG, "%d fish at hunger %.1f, pellets cleared, trickle held (feed / fed / trickle on releases)", n, lvl);
        show_state(t);
    } else if (!strcmp(c, "fed")) {
        float lvl = argc > 1 ? atof(argv[1]) : 1.0f;
        for (int i = 0; i < t->n_fish; i++) t->fish[i].hunger = lvl;
        t->trickle_off = false;
        ESP_LOGI(TAG, "everyone at hunger %.1f, trickle on", lvl);
    } else if (!strcmp(c, "set") && argc >= 3) {
        float lvl = atof(argv[2]);
        int who = argc > 3 ? find_fish(t, argv[3]) : -1;
        if (argc > 3 && who < 0) { ESP_LOGW(TAG, "no fish '%s'", argv[3]); return; }
        int hit = 0;
        for (int i = 0; i < t->n_fish; i++) {
            if (who >= 0 && i != who) continue;
            float *d = drive_of(&t->fish[i], argv[1]);
            if (!d) { ESP_LOGW(TAG, "no drive '%s'", argv[1]); return; }
            *d = lvl < 0 ? 0 : lvl > 10 ? 10 : lvl; hit++;
        }
        ESP_LOGI(TAG, "%s = %.1f for %d fish", argv[1], lvl, hit);
    } else if (!strcmp(c, "feed")) {
        int n = argc > 1 ? atoi(argv[1]) : 3;
        float x = argc > 2 ? atof(argv[2]) : (t->feed_spot_x >= 0 ? t->feed_spot_x : TANK_W * 0.5f);
        t->trickle_off = false;
        tank_feed(t, x, n);
        ESP_LOGI(TAG, "%d pellets at x %.0f (keeper feeding #%d), trickle on", n, x, t->player_feedings);
    } else if (!strcmp(c, "trickle") && argc > 1) {
        t->trickle_off = !strcmp(argv[1], "off");
        ESP_LOGI(TAG, "trickle %s", t->trickle_off ? "OFF" : "on");
    } else if (!strcmp(c, "pellets")) {
        clear_pellets(t); ESP_LOGI(TAG, "pellets cleared");
    } else if (!strcmp(c, "algae") && argc > 1) {
        if (!strcmp(argv[1], "clear")) { memset(t->algae, 0, sizeof t->algae); ESP_LOGI(TAG, "glass clean"); }
        else { int s = atoi(argv[1]); tank_grow_algae(t, s); ESP_LOGI(TAG, "algae +%d steps", s); }
    } else if (!strcmp(c, "veg") && argc > 2) {
        float g = atof(argv[2]);
        if (!strcmp(argv[1], "all")) for (int b = 0; b < tank_veg_beds(t); b++) tank_veg_set(t, b, g);
        else { int b = atoi(argv[1]); if (b >= 0 && b < tank_veg_beds(t)) tank_veg_set(t, b, g); }
        ESP_LOGI(TAG, "veg %s -> %.2f", argv[1], g);
    } else if (!strcmp(c, "light")) {
        tank_toggle_light(t); ESP_LOGI(TAG, "light %s (manual)", t->light_on ? "on" : "off");
    } else if (!strcmp(c, "auto")) {
        tank_light_auto(t); ESP_LOGI(TAG, "light back on the idle rule");
    } else if (!strcmp(c, "sleep") && argc > 1) {
        float h = atof(argv[1]);
        progression_slept(t, h * 3600.0f);      /* growth + the full-night badge, as a real wake would */
        ESP_LOGI(TAG, "slept %.1f h", h);
        show_state(t);
    } else if (!strcmp(c, "imu")) {             /* a short trace of raw polls: is the table really still? */
        int n = argc > 1 ? atoi(argv[1]) : 8; if (n < 1) n = 1; if (n > 40) n = 40;
        for (int i = 0; i < n; i++) {               /* (the console runs in the tank task: poll here, the task is blocked) */
            imu_port_poll(esp_timer_get_time());
            int16_t a[3]; int m; imu_port_last(a, &m);
            ESP_LOGI(TAG, "imu poll: g=[%6d %6d %6d] motion %5d %s", a[0], a[1], a[2], m, imu_port_moving() ? "MOVING" : "still");
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    } else if (!strcmp(c, "settings")) {
        bool on = argc < 2 || strcmp(argv[1], "off");
        touch_port_show_settings(on); ESP_LOGI(TAG, "settings page %s", on ? "up (CLOSE ends it)" : "closed");
    } else if (!strcmp(c, "milestones")) {
        bool on = argc < 2 || strcmp(argv[1], "off");
        touch_port_show_milestones(on); ESP_LOGI(TAG, "milestones page %s", on ? "up (CLOSE button ends it)" : "closed");
    } else if (!strcmp(c, "shop")) {                 /* the sand dollar page */
        bool on = argc < 2 || strcmp(argv[1], "off");
        touch_port_show_shop(on); ESP_LOGI(TAG, "shop page %s", on ? "up (CLOSE ends it)" : "closed");
    } else if (!strcmp(c, "dollars")) {              /* dollars [n]: grant n (negative takes), or just the balance */
        if (argc > 1) progression_sd_grant(t, atoi(argv[1]));
        ESP_LOGI(TAG, "sand dollars %d (earned %d) | colonies %d | %.0f cm trimmed", (int)t->sd_balance, (int)t->sd_earned,
                 (int)t->algae_colonies, t->trim_px / PX_PER_CM);
    } else if (!strcmp(c, "buy") && argc > 1) {      /* buy plant|snail|...|shrimp: the shop's sale, at the price */
        int item = !strcmp(argv[1], "plant") ? 0 : !strcmp(argv[1], "snail") ? 1 : !strcmp(argv[1], "castle") ? 2 : !strcmp(argv[1], "coral") ? 3 : !strcmp(argv[1], "cluster") ? 4 : !strcmp(argv[1], "shrimp") ? 5 : -1;
        if (item < 0) ESP_LOGW(TAG, "buy plant|snail|castle|coral|cluster|shrimp");
        else if (progression_buy(t, item)) ESP_LOGI(TAG, "%s unlocked, %d sand dollars left%s", SD_ITEMS[item].name, (int)t->sd_balance,
                                                    tank_decor_placeable(item) ? " (`place` opens the placement page)" : "");
        else ESP_LOGW(TAG, "%s refused: owned, or %d < %d", SD_ITEMS[item].name, (int)t->sd_balance, SD_ITEMS[item].price);
    } else if (!strcmp(c, "coral") && argc > 1) {    /* coral <0..7|rrggbb> (its colour) | coral grow <0.12..1.25> (its growth, staged; 1 = the fan, 1.25 = the crown) */
        if (!(t->sd_unlocks & SD_ITEM_CORAL)) { ESP_LOGW(TAG, "no coral in the tank (`buy coral`)"); return; }
        if (!strcmp(argv[1], "grow") && argc > 2) {
            float g = (float)atof(argv[2]); if (g < CORAL_START) g = CORAL_START; if (g > CORAL_FULL) g = CORAL_FULL;
            t->coral_growth = g; progression_save(t);
            ESP_LOGI(TAG, "coral growth %.2f (a month to 1.00, the crown by %.2f), saved", g, CORAL_FULL); return;
        }
        uint32_t rgb = strlen(argv[1]) >= 6 ? (uint32_t)strtoul(argv[1], NULL, 16) : CORAL_PAL[atoi(argv[1]) < 0 ? 0 : atoi(argv[1]) >= CORAL_N ? CORAL_N - 1 : atoi(argv[1])];
        tank_coral_set_rgb(t, rgb); progression_save(t);
        ESP_LOGI(TAG, "coral colour %06x, saved", (unsigned)tank_coral_rgb(t));
    } else if (!strcmp(c, "sell") && argc > 1) {     /* sell plant|castle|coral|cluster: the sale back at 20% (never the snail) */
        int item = !strcmp(argv[1], "plant") ? 0 : !strcmp(argv[1], "castle") ? 2 : !strcmp(argv[1], "coral") ? 3 : !strcmp(argv[1], "cluster") ? 4 : -1;
        if (item < 0) ESP_LOGW(TAG, "sell plant|castle|coral|cluster (the snail stays)");
        else if (progression_sell(t, item)) ESP_LOGI(TAG, "%s sold back for %d, balance %d", SD_ITEMS[item].name, progression_sell_value(item), (int)t->sd_balance);
        else ESP_LOGW(TAG, "%s: not in the tank", SD_ITEMS[item].name);
    } else if (!strcmp(c, "cluster") && argc > 1) {  /* cluster look <0-2> | cluster grow <0..2> (1 = full size, 2 = every tentacle) */
        if (!(t->sd_unlocks & SD_ITEM_CLUSTER)) { ESP_LOGW(TAG, "no cluster in the tank (`buy cluster`)"); return; }
        if (!strcmp(argv[1], "look") && argc > 2) { tank_cluster_set_scheme(t, atoi(argv[2])); progression_save(t); ESP_LOGI(TAG, "cluster look %s, saved", CLUSTER_SCHEMES[tank_cluster_scheme(t)].name); }
        else if (!strcmp(argv[1], "grow") && argc > 2) {
            float g = (float)atof(argv[2]); if (g < CLUSTER_START + 1e-4f) g = CLUSTER_START + 1e-4f; if (g > CLUSTER_FULL) g = CLUSTER_FULL;
            t->cluster_growth = g; progression_save(t); ESP_LOGI(TAG, "cluster growth %.2f (1 = full size, %.0f = every tentacle), saved", g, CLUSTER_FULL); }
        else ESP_LOGW(TAG, "cluster look <0-2> | cluster grow <g>");
    } else if (!strcmp(c, "place")) {                /* place [plant|castle|coral|cluster] [x [behind|among|front]]: the piece's spot; no x = the page */
        int item = 0, a = 1;
        if (argc > 1 && !strcmp(argv[1], "castle")) { item = 2; a = 2; }
        else if (argc > 1 && !strcmp(argv[1], "coral")) { item = 3; a = 2; }
        else if (argc > 1 && !strcmp(argv[1], "cluster")) { item = 4; a = 2; }
        else if (argc > 1 && !strcmp(argv[1], "plant")) { a = 2; }
        const char *what = item == 2 ? "castle" : item == 3 ? "coral" : item == 4 ? "cluster" : "plant";
        if (!(t->sd_unlocks & (item == 2 ? SD_ITEM_CASTLE : item == 3 ? SD_ITEM_CORAL : item == 4 ? SD_ITEM_CLUSTER : SD_ITEM_PLANT))) { ESP_LOGW(TAG, "no %s in the tank (`buy %s`)", what, what); return; }
        if (argc <= a) { touch_port_show_shop(false); setup_begin_place(t, item); ESP_LOGI(TAG, "placement page up (drag on the glass, DEPTH, DONE)"); return; }
        int z = tank_decor_z(t, item);
        if (argc > a + 1) z = !strcmp(argv[a + 1], "behind") || !strcmp(argv[a + 1], "back") ? DECOR_Z_BACK : !strcmp(argv[a + 1], "front") ? DECOR_Z_FRONT : DECOR_Z_MIDDLE;
        tank_decor_set(t, item, (float)atof(argv[a]), z); progression_save(t); z = tank_decor_z(t, item);
        ESP_LOGI(TAG, "%s at x %.0f, %s, saved", what, tank_decor_x(t, item),
                 item == 2 ? (z == DECOR_Z_BACK ? "BEHIND the grass" : "IN FRONT of the grass") : z == DECOR_Z_BACK ? "BEHIND the fish" : z == DECOR_Z_FRONT ? "IN FRONT of the fish" : "AMONG the fish");
    } else if (!strcmp(c, "pmic")) {
        if (argc > 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")))
            ESP_LOGI(TAG, "rail %s %s: %s", argv[2], argv[1], battery_port_set_rail(argv[2], !strcmp(argv[1], "on")) ? "ok" : "REFUSED");
        else if (argc > 1 && !strcmp(argv[1], "trim")) battery_port_trim_rails();
        else battery_port_dump();
    } else if (!strcmp(c, "bright") && argc > 1) {
        int v = atoi(argv[1]); if (v < 0) v = 0; if (v > 255) v = 255;
        display_port_set_brightness((uint8_t)v);
        ESP_LOGI(TAG, "brightness %d/255", v);
    } else if (!strcmp(c, "deepsleep")) {
        int n = argc > 1 ? atoi(argv[1]) : 0;
        ESP_LOGI(TAG, "%s - the USB port vanishes until the wake", n > 0 ? "5 s grace, then deep sleep with the timer" : "the keeper's sleep: the grace, then power-off (the PWR key boots it)");
        vTaskDelay(pdMS_TO_TICKS(50));
        device_sleep(n);
    } else if (!strcmp(c, "keytime")) {
        int n = argc > 1 ? atoi(argv[1]) : 15; if (n < 1) n = 1; if (n > 60) n = 60;
        battery_port_key_trace(n);
    } else if (!strcmp(c, "poweroff")) {
        ESP_LOGI(TAG, "power-off now (the PWR key or USB boots it) - the USB port vanishes");
        vTaskDelay(pdMS_TO_TICKS(50));
        device_poweroff();
    } else if (!strcmp(c, "snd")) {
        /* snd <cue> [pitch_q8] | snd off|quiet|normal | snd stop <cue> | snd list | snd settle <codec ms> <amp ms> | snd idle <s> */
        if (argc < 2 || !strcmp(argv[1], "list")) {
            ESP_LOGI(TAG, "audio %s, volume %d (0 off 1 quiet 2 normal), imu motion %d (%s)", audio_port_state(), audio_port_volume(), imu_port_motion(), imu_port_moving() ? "moving" : "still");
            for (int i = 0; i < SND_COUNT; i++)
                ESP_LOGI(TAG, "  %-13s %s%s", SND_CUES[i].name, SND_CUES[i].n_var ? "ready" : "deferred", SND_CUES[i].loop ? " (loop)" : "");
        } else if (!strcmp(argv[1], "off") || !strcmp(argv[1], "quiet") || !strcmp(argv[1], "normal")) {
            audio_port_set_volume(!strcmp(argv[1], "off") ? 0 : !strcmp(argv[1], "quiet") ? 1 : 2);
        } else if (!strcmp(argv[1], "stop") && argc > 2) {
            int id = audio_cue_by_name(argv[2]); if (id >= 0) audio_port_stop(id); else ESP_LOGW(TAG, "no cue %s", argv[2]);
        } else if (!strcmp(argv[1], "settle") && argc > 3) {
            audio_port_tune(atoi(argv[2]), atoi(argv[3]), 0); ESP_LOGI(TAG, "snd settle %s %s", argv[2], argv[3]);
        } else if (!strcmp(argv[1], "idle") && argc > 2) {          /* 0 = warm while awake, N = auto-off after N s of silence */
            audio_port_tune(-1, -1, atoi(argv[2])); ESP_LOGI(TAG, "snd idle %s", argv[2]);
        } else if (!strcmp(argv[1], "battery")) {
            notice_low_battery(); ESP_LOGI(TAG, "low-battery notice queued");
        } else {
            int id = audio_cue_by_name(argv[1]);
            if (id < 0) { ESP_LOGW(TAG, "no cue %s (snd list)", argv[1]); return; }
            audio_port_play(id, argc > 2 ? atoi(argv[2]) : AUDIO_PITCH_ONE);
            ESP_LOGI(TAG, "snd %s%s", argv[1], SND_CUES[id].n_var ? "" : " (deferred: silent)");
        }
    } else if (!strcmp(c, "codec")) {
        codec_port_dump();
    } else if (!strcmp(c, "batlog")) {
        if (argc > 1 && !strcmp(argv[1], "clear")) { batlog_clear(); ESP_LOGI(TAG, "battery log cleared"); }
        else batlog_print();
    } else if (!strcmp(c, "level") && argc > 1) {
        if (!brightness_set_level(atoi(argv[1]))) ESP_LOGW(TAG, "level is 100, 60 or 30");
    } else if (!strcmp(c, "reset")) {
        if (argc < 2) { touch_port_confirm_open(); return; }
        int ans = !strcasecmp(argv[1], "yes") ? 1 : !strcasecmp(argv[1], "no") ? -1 : 0;
        if (!ans) { ESP_LOGW(TAG, "reset [yes|no]"); return; }
        if (!touch_port_confirm_answer(ans)) ESP_LOGW(TAG, "no reset prompt is up (`reset` first)");
        else ESP_LOGI(TAG, "reset prompt: %s", ans > 0 ? "YES - the tank task wipes it this frame" : "NO");
    } else if (!strcmp(c, "setup")) {
        if (argc > 1 && !strcmp(argv[1], "off")) { setup_cancel(t); ESP_LOGI(TAG, "setup panel dropped%s", progression_setup_pending() || progression_newborn() >= 0 ? " (still owed: it returns at the next boot)" : ""); }
        else { setup_begin(t); ESP_LOGI(TAG, "setup: welcome page up (tap through on the glass)"); }
    } else if (!strcmp(c, "battery")) {              /* battery <pct> [charging|full|plugged]|real|page [off]: a staged gauge, the page */
        if (argc > 1 && !strcmp(argv[1], "page")) {
            bool on = !(argc > 2 && !strcmp(argv[2], "off"));
            touch_port_show_battery(on); ESP_LOGI(TAG, "battery page %s", on ? "up (any tap closes it; 30 s by itself)" : "closed");
        } else if (argc > 1 && strcmp(argv[1], "real")) {
            int st = argc > 2 ? (!strcmp(argv[2], "charging") ? BAT_CHARGING : !strcmp(argv[2], "full") ? BAT_FULL : !strcmp(argv[2], "plugged") ? BAT_PLUGGED : BAT_ON_BATTERY) : BAT_ON_BATTERY;
            device_fake_battery(atoi(argv[1]), st);
            ESP_LOGI(TAG, "gauge STAGED at %d%% %s (`battery real` ends it)", atoi(argv[1]),
                     st == BAT_CHARGING ? "CHARGING: the bolt, the sweep, the pill for a few seconds" : st == BAT_FULL ? "on the cable, FULL: the bolt, still"
                     : st == BAT_PLUGGED ? "on the cable, NOT CHARGING: the gray bolt" : "on battery (10 or less: the notice, the cue, the pill stays up)");
        } else if (argc > 1) { device_fake_battery(-1, BAT_ON_BATTERY); ESP_LOGI(TAG, "the real gauge (battery <pct> [charging|full|plugged] stages one)"); }
        else device_battery_log();
    } else if (!strcmp(c, "kbd")) {                  /* the name page's rejected designs, to be shown: kbd wheel|grid|pages */
        if (argc > 1) setup_set_keyboard(!strcmp(argv[1], "grid") ? SETUP_KBD_GRID : !strcmp(argv[1], "pages") ? SETUP_KBD_PAGES : SETUP_KBD_WHEEL);
        ESP_LOGI(TAG, "name page: %s", setup_keyboard() == SETUP_KBD_GRID ? "GRID (the first cut: 7 x 4 keys on a panel)" :
                 setup_keyboard() == SETUP_KBD_PAGES ? "PAGES (the second: half the alphabet, big keys)" : "the letter wheel");
    } else if (!strcmp(c, "touch")) {
        if (argc > 2 && !strcmp(argv[1], "bias")) touch_port_set_bias(atoi(argv[2]));
        ESP_LOGI(TAG, "touch bias %d px (reported y - %d)", touch_port_bias(), touch_port_bias());
    } else if (!strcmp(c, "name") && argc > 2) {
        int who = find_fish(t, argv[1]); if (who < 0) { ESP_LOGW(TAG, "no fish '%s'", argv[1]); return; }
        tank_set_name(t, who, argv[2]); progression_save(t);
        ESP_LOGI(TAG, "fish %d is now %s (saved)", who, t->fish[who].name);
    } else if (!strcmp(c, "save")) {
        progression_save(t); ESP_LOGI(TAG, "saved");
    } else if (!strcmp(c, "stash")) {
        stash(t);
    } else if (!strcmp(c, "restore")) {
        if (!nvs_has("bk")) { ESP_LOGW(TAG, "nothing stashed"); return; }
        if (setup_active()) setup_cancel(t);         /* a page left up (the placement page) would outlive the tank it was about */
        touch_port_show_shop(false); touch_port_show_milestones(false); touch_port_show_settings(false);
        if (!nvs_copy("bk", "save")) { ESP_LOGW(TAG, "restore copy failed"); return; }
        nvs_drop("bk");
        tank_init(t, (uint32_t)esp_timer_get_time() ^ 0xC0FFEEu);
        progression_boot(t);
        ESP_LOGI(TAG, "real tank restored (%d fish)%s", t->n_fish,
                 t->ravenous ? " - parked over an hour, so it woke ravenous: `fed` if unwanted" : "");
        show_state(t);
    } else if (!strcmp(c, "fresh")) {
        if (!stage_guard(t)) return;
        tank_init(t, (uint32_t)esp_timer_get_time() ^ 0xC0FFEEu);
        progression_fresh(t);
        progression_setup_done(t);       /* a staged scene, not a keeper's new tank: no welcome (`setup` cues it) */
        ESP_LOGI(TAG, "fresh tank: %s + %s, both fry", t->fish[0].name, t->fish[1].name);
        show_state(t);
    } else if (!strcmp(c, "stages")) {
        if (!stage_guard(t)) return;
        int n = t->n_fish < 4 ? t->n_fish : 4;
        for (int i = 0; i < n; i++) progression_set_age(t, i, STAGE_AGE[4 - n + i]);
        ESP_LOGI(TAG, "one fish per stage");
        show_state(t);
    } else if (!strcmp(c, "stage") && argc > 2) {
        int st = stage_of(argv[2]);
        if (st < 0) { ESP_LOGW(TAG, "stage is fry|juv|adult|elder"); return; }
        if (!stage_guard(t)) return;
        if (!strcmp(argv[1], "all")) for (int i = 0; i < t->n_fish; i++) progression_set_age(t, i, STAGE_AGE[st]);
        else { int who = find_fish(t, argv[1]); if (who < 0) { ESP_LOGW(TAG, "no fish '%s'", argv[1]); return; }
               progression_set_age(t, who, STAGE_AGE[st]); }
        show_state(t);
    } else if (!strcmp(c, "overgrown")) {
        if (!stage_guard(t)) return;
        for (int b = 0; b < VEG_BEDS; b++) tank_veg_set(t, b, 0.95f);
        tank_grow_algae(t, 600);
        ESP_LOGI(TAG, "overgrown: grass at the ceiling, glass fouled - stress settles toward ~6.7 over the next minute; swipe to trim, wipe to clean");
        show_state(t);
    } else if (!strcmp(c, "court")) {
        if (!stage_guard(t)) return;
        make_room(t);
        if (tank_nursery_bed(t) < 0) { tank_veg_set(t, 0, 0.3f); ESP_LOGI(TAG, "no nursery: reef bed set to 0.30"); }
        progression_stage_arrival(t);
        ESP_LOGI(TAG, "arrival staged: in %.0f-%.0f s the parents swim down into the nursery grass and court; the fry is born after %.0f s of circling there together (a page up pauses it; `arrive` = now)",
                 SPAWN_WAIT_MIN_S, SPAWN_WAIT_MAX_S, SPAWN_DANCE_S);
    } else if (!strcmp(c, "arrive")) {
        if (!stage_guard(t)) return;
        make_room(t);
        if (tank_nursery_bed(t) < 0) { tank_veg_set(t, 0, 0.3f); ESP_LOGI(TAG, "no nursery: reef bed set to 0.30"); }
        int before = t->n_fish;
        progression_force_arrival(t);
        if (t->n_fish > before) ESP_LOGI(TAG, "a fry: %s, by the reef - the birth flow opens (announce, name, family; `setup off` drops it)", t->fish[t->n_fish - 1].name);
        else ESP_LOGW(TAG, "no arrival: tank at the cap (%d)", t->n_fish);
        show_state(t);
    } else if (!strcmp(c, "age") && argc > 2) {
        int who = find_fish(t, argv[1]); if (who < 0) { ESP_LOGW(TAG, "no fish '%s'", argv[1]); return; }
        if (!stage_guard(t)) return;
        progression_set_age(t, who, atof(argv[2]) * 3600.0f);
        show_state(t);
    } else {
        ESP_LOGW(TAG, "unknown: '%s' (help)", c);
    }
}

void director_init(void) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    s_ok = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
    ESP_LOGI(TAG, "%s", s_ok ? "console ready (type help)" : "USB serial driver failed: console off");
    if (nvs_has("bk")) ESP_LOGW(TAG, "a STAGED tank is up: the real tank is parked (restore brings it back)");
#else
    ESP_LOGI(TAG, "no USB serial: console off");
#endif
}

void director_poll(tank_t *t) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    if (!s_ok) return;
    uint8_t buf[32]; int n;
    while ((n = usb_serial_jtag_read_bytes(buf, sizeof buf, 0)) > 0) {
        for (int i = 0; i < n; i++) {
            char ch = (char)buf[i];
            if (ch == '\n' || ch == '\r') {
                if (s_len) { s_line[s_len] = 0; run(t, s_line); }
                s_len = 0;
            } else if (s_len < (int)sizeof s_line - 1) s_line[s_len++] = ch;
        }
    }
#else
    (void)t;
#endif
}
