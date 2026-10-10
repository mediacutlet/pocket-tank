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
#include "progression.h"
#include "setup.h"
#include "director.h"
#include "update.h"
#include "version.h"
#include "esp_ota_ops.h"
const char *net_port_manifest_url(void);
bool net_port_set_manifest_url(const char *url);
#include "touch_port.h"
#include "battery_port.h"
#include "display_port.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "brightness.h"
#include "batlog.h"
#include "codec_port.h"
#include "audio_port.h"
#include "imu_port.h"
#include "audio.h"
#include "notice.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_system.h"
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

/* a species by its token or its shop word (seahorse, puffer, eel, shark, crab ...), or -1 */
static int species_of(const char *s) {
    for (int sp = 1; sp < SP_COUNT; sp++) {
        if (!strcasecmp(s, SPECIES[sp].token) || !strcasecmp(s, SPECIES[sp].name)) return sp;
        const char *words[2] = { SD_ITEMS[SD_ITEM_SP_FIRST + sp - 1].name, SPECIES[sp].token }; size_t ls = strlen(s);
        for (int w = 0; w < 2; w++) {                                                  /* the shop's word or the token, and their plurals */
            const char *nm = words[w]; size_t ln = strlen(nm);
            if (!strcasecmp(s, nm)) return sp;
            if (ls == ln + 1 && (s[ln] | 32) == 's' && !strncasecmp(s, nm, ln)) return sp;        /* "crabs", "sharks" */
            if (ls == ln + 2 && !strcasecmp(s + ln, "es") && !strncasecmp(s, nm, ln)) return sp;   /* "octopuses" */
        }
    }
    if (!strcasecmp(s, "pufferfish")) return SP_PUFFER;
    if (!strcasecmp(s, "anglerfish")) return SP_ANGLER;
    if (!strcasecmp(s, "hammerhead")) return SP_SHARK;
    return -1;
}
static void clear_pellets(tank_t *t) {
    for (int i = 0; i < MAX_FOOD; i++) t->food[i].alive = false;
}

static void show_state(const tank_t *t) {
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        ESP_LOGI(TAG, "%d %-7s %-8s/%d %-5s age %.1fh size %.2f hunger %.1f energy %.1f stress %.1f curiosity %.1f trust %.1f ms %03x  %s at %.0f,%.0f",
                 i, f->name, tank_species(f)->token, f->variant, STAGE_NAMES[f->stage], progression_age_s(t, i) / 3600.0f, f->size,
                 f->hunger, f->energy, f->stress, f->curiosity, f->trust, (unsigned)f->ms_bits,
                 GOAL_NAMES[f->goal.id], f->x, f->y);
    }
    int pellets = 0, cells = 0;
    for (int i = 0; i < MAX_FOOD; i++) pellets += t->food[i].alive;
    for (int i = 0; i < ALGAE_CELLS; i++) cells += t->algae[i] > 0;
    ESP_LOGI(TAG, "pellets %d | trickle %s | ravenous %d | %s%s idle %.0fs | veg %.2f %.2f %.2f | algae cells %d/%d | courting %s%s%s%s | arrival %s",
             pellets, t->trickle_off ? "OFF" : t->autofeed_off ? "off (AUTO FEED off)" : "on", (int)t->ravenous,
             t->night ? "night" : "day", t->light_override ? " (manual)" : t->orient_lock ? " (rotation locked)" : "",
             t->idle_s,
             t->veg_growth[0], t->veg_growth[1], t->veg_growth[2], cells, ALGAE_CELLS,
             t->courting ? t->fish[t->court_a].name : "no", t->courting ? "+" : "",
             t->courting ? t->fish[t->court_b].name : "", t->spawning ? " (SPAWNING)" : t->court_active > 0 ? " (circling)" : "",
             progression_arrival_pending() ? "staged" : "-");
    ESP_LOGI(TAG, "sand dollars %d (earned %d) | shop:%s%s%s%s%s%s%s%s%s%s%s | colonies %d | %.0f cm trimmed",
             (int)t->sd_balance, (int)t->sd_earned, t->sd_unlocks & SD_ITEM_PLANT ? " plant" : "", t->sd_unlocks & SD_ITEM_SNAIL ? " snail" : "",
             t->sd_unlocks & SD_ITEM_CASTLE ? " castle" : "", t->sd_unlocks & SD_ITEM_CORAL ? " coral" : "", t->sd_unlocks & SD_ITEM_CLUSTER ? " cluster" : "",
             t->sd_unlocks & SD_ITEM_SHRIMP ? " shrimp" : "", t->sd_unlocks & SD_ITEM_URCHIN ? " urchin" : "",
             t->sd_unlocks & SD_ITEM_WRECK ? " wreck" : "", t->sd_unlocks & SD_ITEM_FROGMAN ? " frogman" : "", t->sd_unlocks & SD_ITEM_SUB ? " submarine" : "", t->sd_unlocks ? "" : " -", (int)t->algae_colonies, t->trim_px / PX_PER_CM);
    {   /* who lives here, by species (2026-10-05): "fish 4, seahorse 2" - and which pairs the shop holds back */
        char sl[160] = ""; size_t l = 0;
        for (int sp = 0; sp < SP_COUNT && l + 24 < sizeof sl; sp++) {
            int n = tank_species_n(t, sp);
            if (n) l += snprintf(sl + l, sizeof sl - l, "%s%s %d", l ? ", " : "", SPECIES[sp].token, n);
        }
        ESP_LOGI(TAG, "species: %s | %d of %d places | courting pair of one species: %s", sl, t->n_fish, POP_CAP, t->courting ? "yes" : "no");
    }
    if (t->sd_unlocks & SD_ITEM_URCHIN)                 /* where it is, what it is after */
        ESP_LOGI(TAG, "urchin at x %.0f | %s | appetite %.3f | %.0f cm grazed so far (keeps the grass over %.2f)", t->urchin_x,
                 tank_urchin_chewing(t) ? "chewing" : t->urchin_frond >= 0 ? "off to the tall grass" : t->urchin_rest > 0 ? "resting" : "ambling",
                 t->urchin_appetite, t->urchin_grazed_px / PX_PER_CM, (double)URCHIN_KEEP);
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
    if (t->sd_unlocks & SD_ITEM_WRECK)
        ESP_LOGI(TAG, "wreck placed: centre x %.0f (%s), %s", tank_decor_x(t, SD_ITEM_WRECK_IDX), t->wreck_x > 0 ? "the keeper's" : "the default",
                 t->wreck_z == DECOR_Z_BACK ? "BEHIND the grass" : "IN FRONT of the grass (the fish pass behind its holes)");
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
    ESP_LOGI(TAG, "shop [off] (the sand dollar page) | dollars [n] (grant n; the balance and the chore counts) | buy plant|snail|castle|coral|cluster|shrimp|urchin|wreck|frogman|<species> (at the price; a species = one juvenile: seahorse octopus puffer angler eel shark squid crab lobster jellyfish) | spawn <species> (a STAGED pair, free) | place [plant|castle|coral|cluster|wreck] [x [behind|among|front]] (the piece's spot; no x = the page; the castle has no among) | coral <0-7|rrggbb> (its colour) | coral grow <g> (its growth, 1 = the fan, 1.25 = the crown) | cluster look <0-2> | cluster grow <g> (1 = full size, 2 = every tentacle) | sell plant|castle|coral|cluster|wreck (20%% back)");
    ESP_LOGI(TAG, "reset (the keeper's confirm prompt, as BOOT + tap opens it) | reset yes|no (answer it here) - YES WIPES EVERY SAVE, a parked tank too");
    ESP_LOGI(TAG, "setup [off] (the first-run flow: welcome, names, colours; off drops the panel - the birth flow too) | name <fish|idx> <newname> (up to %d letters, saved)", FISH_NAME_MAX);
    ESP_LOGI(TAG, "battery <pct> [charging|full|plugged]|real (a STAGED gauge: on battery at pct - the card's pill, and at 10 or less the low-battery notice + cue + the pill that stays - or on the cable: the bolt, the sweep while charging, the pill for a few seconds; not saved) | battery page [off] (the battery page, as a tap on the pill opens it) | battery (its numbers) | snd battery (just the notice + cue)");
    ESP_LOGI(TAG, "kbd [wheel|grid|pages] (the name page's design: the wheel, or one of the two rejected keyboards of 09-13 - not saved, a boot is the wheel)");
    ESP_LOGI(TAG, "touch [bias <px>] [log on|off] [lift <ms>] [said on|off] (finger-landing correction: reported touches move up by px; not saved; also the raw span seen since boot)");
    ESP_LOGI(TAG, "pmic (AXP2101 dump) | pmic on|off <aldo1|aldo2..4|bldo1|bldo2|cpusldo|dcdc2..5|dldo1|dldo2> (experiments; boot trims the unused ones) | pmic trim");
    ESP_LOGI(TAG, "bright <0-255> (panel now; not saved) | level 100|60|30 (the keeper's setting, saved)");
    ESP_LOGI(TAG, "batlog [clear] (the tank's own battery log: SoC/VBAT every 5 min awake, 30 min asleep, mA derived - read it after a night on battery) | codec (ES8311 registers) | clock (the clock: RTC chip or the internet's time, and whether tonight is a power-off) | sleepcfg [mask] (the round board's deep-sleep savings: 1 touch, 2 panel, 4 IMU, 8 QSPI held; 15 = all, the default) | deepsleep [N] (N: 5 s grace then deep sleep with an N s timer wake - one batlog window per N, BOOT wakes it; no N: the keeper's sleep, grace then power-off) | poweroff (save + PMIC cut now) | keytime [N] (N s of timing every PWR press - is a tap under the PMIC's 128 ms power-on hold?)");
    ESP_LOGI(TAG, "overgrown (grass to the ceiling + fouled glass; fish stress climbs) | court (stage the fry: in ~10-20 s the pair courts in the grass, then it is born) | arrive (the fry, now)");
    ESP_LOGI(TAG, "wifi (the saved network) | wifi set ssid=<name> pass=<secret> | wifi forget | ota (slots, channel) | ota check (save + restart into update mode, as CHECK FOR UPDATES) | ota url <manifest url>|default (the test channel)");
}

/* `wifi set ssid=<name> pass=<secret>`: the raw line, since a name or a
 * passphrase may hold spaces (everything after "pass=" is the passphrase) */
static void wifi_set_raw(const char *args) {
    const char *sp = strstr(args, "ssid="), *pp = strstr(args, " pass=");
    if (!sp) { ESP_LOGW(TAG, "wifi set ssid=<name> pass=<secret> (pass= may be left out for an open network)"); return; }
    char ssid[NET_SSID_MAX + 1] = "", pass[NET_PASS_MAX + 1] = "";
    const char *s0 = sp + 5, *s1 = pp && pp > s0 ? pp : s0 + strlen(s0);
    snprintf(ssid, sizeof ssid, "%.*s", (int)(s1 - s0), s0);
    if (pp) snprintf(pass, sizeof pass, "%s", pp + 6);
    for (char *e = ssid + strlen(ssid); e > ssid && e[-1] == ' '; e--) e[-1] = 0;
    if (!ssid[0]) { ESP_LOGW(TAG, "wifi set: empty name"); return; }
    net_port_creds_set(ssid, pass);
}
static void imp_dump(void);
static void run(tank_t *t, char *line) {
    if (!strncmp(line, "wifi set ", 9)) { wifi_set_raw(line + 9); return; }
    if (!strcmp(line, "improv")) { imp_dump(); return; }     /* the handshake's flight recorder */
    if (!strncmp(line, "ota tap ", 8)) {                     /* update mode, hands-free: a tap at (x, y) in tank coordinates */
        float x = 0, y = 0;
        if (sscanf(line + 8, "%f %f", &x, &y) == 2 && update_active()) {
            int h = update_hit(x, y);
            update_touch(x, y, true); update_touch(x, y, false);
            ESP_LOGI(TAG, "ota tap %.0f,%.0f -> %s (page %d now)", x, y, update_hit_name(h), update_page());
        } else ESP_LOGW(TAG, "ota tap <x> <y> (update mode only)");
        return;
    }
    if (!strncmp(line, "ota url ", 8)) {                     /* the manifest to check (the test channel); "default" = the release channel */
        const char *u = line + 8; while (*u == ' ') u++;
        bool ok = net_port_set_manifest_url(strcmp(u, "default") ? u : NULL);
        ESP_LOGI(TAG, "ota url %s: %s", ok ? "set" : "NOT set", net_port_manifest_url());
        return;
    }
    char *argv[6]; int argc = 0;
    for (char *tok = strtok(line, " \t"); tok && argc < 6; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    if (!argc) return;
    for (char *p = argv[0]; *p; p++) *p = (char)tolower((unsigned char)*p);
    const char *c = argv[0];
    if (!t && strcmp(c, "wifi") && strcmp(c, "ota") && strcmp(c, "help")) {   /* update mode: no tank to command */
        ESP_LOGW(TAG, "update mode: only wifi / ota / help (ota tap <x> <y>, ota page)"); return; }
    if (!strcmp(c, "help")) help();
    else if (!strcmp(c, "state")) show_state(t);
    else if (!strcmp(c, "wifi")) {                            /* the saved network (docs/OTA.md) */
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        if (argc > 1 && !strcmp(argv[1], "forget")) { net_port_creds_forget(); return; }
        if (net_port_creds_get(ssid, pass)) ESP_LOGI(TAG, "network: %s (%s)", ssid, pass[0] ? "with a password" : "open");
        else ESP_LOGI(TAG, "no network saved (wifi set ssid=<name> pass=<secret>, or the wizard on the glass)");
    } else if (!strcmp(c, "ota")) {                           /* update mode and its channel */
        const esp_partition_t *run = esp_ota_get_running_partition(), *boot = esp_ota_get_boot_partition();
        if (argc > 1 && !strcmp(argv[1], "check")) { if (!t) { ESP_LOGW(TAG, "already in update mode"); return; }
                                                      ESP_LOGI(TAG, "ota check: saving, restarting into update mode"); device_update_check(); return; }
        if (argc > 1 && !strcmp(argv[1], "calib")) { update_debug_calib(); ESP_LOGI(TAG, "ota calib: nine crosshairs, a press each (the log fits the mapping)"); return; }
        if (argc > 2 && !strcmp(argv[1], "flip")) {            /* update mode never reads the IMU: the crosshairs with the board the other way up */
            if (t) { ESP_LOGW(TAG, "ota flip: update mode only (the tank turns with the IMU)"); return; }
            bool inv = !strcmp(argv[2], "on"); display_port_set_inverted(inv); touch_port_set_inverted(inv);
            ESP_LOGI(TAG, "ota flip %s: the picture is %s", inv ? "on" : "off", inv ? "turned over" : "upright"); return; }
        if (argc > 1 && !strcmp(argv[1], "kbd")) { update_debug_keyboard(); ESP_LOGI(TAG, "ota kbd: the keyboard page (calibration; the saved network is untouched)"); return; }
        if (argc > 1 && !strcmp(argv[1], "page")) { ESP_LOGI(TAG, "update mode: %s, page %d, %d characters typed", update_active() ? "active" : "not active", update_page(), (int)strlen(update_typed())); return; }   /* never the text: it may be the saved passphrase */
        ESP_LOGI(TAG, "v%s %s (build %s) running from %s, next boot %s | manifest %s | ota check (= CHECK FOR UPDATES) | ota url <url>|default",
                 PT_RELEASE, PT_RELEASE_STAGE, version_port_string(), run ? run->label : "?", boot ? boot->label : "?", net_port_manifest_url());
    }
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
    } else if (!strcmp(c, "screen")) {          /* a worn tank's way up (tank.h tank_screen_*): screen [normal|turned] */
        if (argc > 1) { tank_screen_set(t, !strcmp(argv[1], "turned")); progression_settings_changed(); }
        ESP_LOGI(TAG, "screen: %s%s", tank_screen_turned(t) ? "TURNED" : "NORMAL", TANK_WORN ? "" : " - not a worn build: never turned");
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
    } else if (!strcmp(c, "buy") && argc > 1) {      /* buy plant|snail|...|shrimp|<species>: the shop's sale, at the price */
        int item = !strcmp(argv[1], "plant") ? 0 : !strcmp(argv[1], "snail") ? 1 : !strcmp(argv[1], "castle") ? 2 : !strcmp(argv[1], "coral") ? 3 : !strcmp(argv[1], "cluster") ? 4 : !strcmp(argv[1], "shrimp") ? 5 : !strcmp(argv[1], "urchin") ? 6 : !strcmp(argv[1], "wreck") ? SD_ITEM_WRECK_IDX : !strcmp(argv[1], "frogman") ? SD_ITEM_FROGMAN_IDX : !strcmp(argv[1], "submarine") ? SD_ITEM_SUB_IDX : -1;
        if (item < 0 && species_of(argv[1]) > 0) item = SD_ITEM_SP_FIRST + species_of(argv[1]) - 1;   /* a species' pair */
        if (item < 0) ESP_LOGW(TAG, "buy plant|snail|castle|coral|cluster|shrimp|urchin|wreck|frogman|submarine|seahorse|octopus|puffer|angler|eel|shark|squid|crab|lobster|jellyfish");
        else if (progression_item_species(item) > 0 && !progression_has_room_one(t)) ESP_LOGW(TAG, "%s refused: no room (%d of %d)", SD_ITEMS[item].name, t->n_fish, POP_CAP);
        else if (progression_buy(t, item)) ESP_LOGI(TAG, "%s unlocked, %d sand dollars left%s", SD_ITEMS[item].name, (int)t->sd_balance,
                                                    tank_decor_placeable(item) ? " (`place` opens the placement page)" : "");
        else ESP_LOGW(TAG, "%s refused: owned, or %d < %d", SD_ITEMS[item].name, (int)t->sd_balance, SD_ITEMS[item].price);
    } else if (!strcmp(c, "spawn") && argc > 1) {    /* spawn <species>: a STAGED pair of juveniles, free (the real tank is parked first) */
        int sp = species_of(argv[1]);
        if (sp < 0) { ESP_LOGW(TAG, "spawn seahorse|octopus|puffer|angler|eel|shark|squid|crab|lobster|jellyfish"); return; }
        if (!stage_guard(t)) return;
        if (progression_spawn_pair(t, sp)) { ESP_LOGI(TAG, "a pair of %s by the reef (staged)", SD_ITEMS[SD_ITEM_SP_FIRST + sp - 1].name); show_state(t); }
        else ESP_LOGW(TAG, "no room for two (%d of %d)", t->n_fish, POP_CAP);
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
    } else if (!strcmp(c, "fish")) {                 /* fish | fish card <n> | fish rename <n> [name] | fish sell <n>: the milestones card's buttons */
        int n = argc > 2 ? atoi(argv[2]) : -1;
        if (argc > 2 && (n < 0 || n >= t->n_fish)) { ESP_LOGW(TAG, "fish %d: the tank has 0..%d", n, t->n_fish - 1); return; }
        if (argc > 2 && !strcmp(argv[1], "card")) { touch_port_show_milestones(true); render_milestones_show_fish(t, n); ESP_LOGI(TAG, "%s's card up", t->fish[n].name); }
        else if (argc > 3 && !strcmp(argv[1], "rename")) { tank_set_name(t, n, argv[3]); progression_save(t); ESP_LOGI(TAG, "fish %d is %s now, saved", n, t->fish[n].name); }
        else if (argc > 2 && !strcmp(argv[1], "rename")) { touch_port_dismiss(); setup_begin_rename(t, n); ESP_LOGI(TAG, "renaming %s: the wheel is up (CANCEL / DONE)", t->fish[n].name); }
        else if (argc > 2 && !strcmp(argv[1], "sell")) {
            int worth = progression_fish_value(t, n); char nm[FISH_NAME_MAX + 1]; snprintf(nm, sizeof nm, "%s", t->fish[n].name);
            if (progression_sell_fish(t, n)) ESP_LOGI(TAG, "%s sold for %d: %d fish left, balance %d", nm, worth, t->n_fish, (int)t->sd_balance);
            else ESP_LOGW(TAG, "%s stays: the tank keeps %d fish, and a fry owed its welcome holds every sale", nm, FISH_KEEP_MIN);
        } else {
            for (int i = 0; i < t->n_fish; i++)
                ESP_LOGI(TAG, "fish %d: %s, %s, trust %.1f, worth %d%s", i, t->fish[i].name, STAGE_NAMES[t->fish[i].stage & 3], t->fish[i].trust,
                         progression_fish_value(t, i), progression_fish_sellable(t, i) ? "" : " (not for sale)");
            fry_req_t rq[FRY_REQ_MAX]; bool staged; int k = progression_next_fry(t, rq, &staged);
            for (int i = 0; i < k; i++) ESP_LOGI(TAG, "next fry: %s - %s %s (%s)%s", rq[i].title, rq[i].words, rq[i].words2, rq[i].progress, rq[i].met ? " MET" : "");
        }
    } else if (!strcmp(c, "sell") && argc > 1) {     /* sell plant|castle|coral|cluster: the sale back at 20% (never the snail) */
        int item = !strcmp(argv[1], "plant") ? 0 : !strcmp(argv[1], "castle") ? 2 : !strcmp(argv[1], "coral") ? 3 : !strcmp(argv[1], "cluster") ? 4 : !strcmp(argv[1], "wreck") ? SD_ITEM_WRECK_IDX : -1;
        if (item < 0) ESP_LOGW(TAG, "sell plant|castle|coral|cluster|wreck (the snail stays)");
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
        else if (argc > 1 && !strcmp(argv[1], "wreck")) { item = SD_ITEM_WRECK_IDX; a = 2; }
        else if (argc > 1 && !strcmp(argv[1], "plant")) { a = 2; }
        const char *what = item == 2 ? "castle" : item == 3 ? "coral" : item == 4 ? "cluster" : item == SD_ITEM_WRECK_IDX ? "wreck" : "plant";
        if (!(t->sd_unlocks & (item == 2 ? SD_ITEM_CASTLE : item == 3 ? SD_ITEM_CORAL : item == 4 ? SD_ITEM_CLUSTER : item == SD_ITEM_WRECK_IDX ? SD_ITEM_WRECK : SD_ITEM_PLANT))) { ESP_LOGW(TAG, "no %s in the tank (`buy %s`)", what, what); return; }
        if (argc <= a) { touch_port_show_shop(false); setup_begin_place(t, item); ESP_LOGI(TAG, "placement page up (drag on the glass, DEPTH, DONE)"); return; }
        int z = tank_decor_z(t, item);
        if (argc > a + 1) z = !strcmp(argv[a + 1], "behind") || !strcmp(argv[a + 1], "back") ? DECOR_Z_BACK : !strcmp(argv[a + 1], "front") ? DECOR_Z_FRONT : DECOR_Z_MIDDLE;
        tank_decor_set(t, item, (float)atof(argv[a]), z); progression_save(t); z = tank_decor_z(t, item);
        ESP_LOGI(TAG, "%s at x %.0f, %s, saved", what, tank_decor_x(t, item),
                 item == 2 || item == SD_ITEM_WRECK_IDX ? (z == DECOR_Z_BACK ? "BEHIND the grass" : "IN FRONT of the grass") : z == DECOR_Z_BACK ? "BEHIND the fish" : z == DECOR_Z_FRONT ? "IN FRONT of the fish" : "AMONG the fish");
    } else if (!strcmp(c, "pmic")) {
        if (argc > 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")))
            ESP_LOGI(TAG, "rail %s %s: %s", argv[2], argv[1], battery_port_set_rail(argv[2], !strcmp(argv[1], "on")) ? "ok" : "REFUSED");
        else if (argc > 1 && !strcmp(argv[1], "trim")) battery_port_trim_rails();
        else battery_port_dump();
    } else if (!strcmp(c, "bright") && argc > 1) {
        int v = atoi(argv[1]); if (v < 0) v = 0; if (v > 255) v = 255;
        display_port_set_brightness((uint8_t)v);
        ESP_LOGI(TAG, "brightness %d/255", v);
    } else if (!strcmp(c, "clock")) {
        device_clock_log();
    } else if (!strcmp(c, "sleepcfg")) {
        int m = device_sleep_cfg(argc > 1 ? atoi(argv[1]) : -1);
        ESP_LOGI(TAG, "sleepcfg %d: the deep sleep turns off%s%s%s%s%s (1 touch chip asleep, 2 panel deep standby, 4 IMU clock, 8 QSPI lines held low; 0 = as the first night)",
                 m, m & 1 ? " +touch" : "", m & 2 ? " +panel" : "", m & 4 ? " +imu" : "", m & 8 ? " +qspi held low" : "", m ? "" : " nothing extra");
    } else if (!strcmp(c, "deepsleep")) {
        int n = argc > 1 ? atoi(argv[1]) : 0;
        ESP_LOGI(TAG, "%s - the USB port vanishes until the wake", n > 0 ? "5 s grace, then deep sleep with the timer" : "the keeper's sleep: the grace, then power-off (the PWR key boots it)");
        vTaskDelay(pdMS_TO_TICKS(50));
        device_sleep(n);
    } else if (!strcmp(c, "keytime")) {
        int n = argc > 1 ? atoi(argv[1]) : 15; if (n < 1) n = 1; if (n > 60) n = 60;
        battery_port_key_trace(n);
    } else if (!strcmp(c, "pwrpin")) {               /* the round board's PWR-key sense line (GPIO 3): its level for N s, a line per change */
        const int sense = board_pwr_sense_pin();
        if (sense < 0) { ESP_LOGI(TAG, "pwrpin: this board has no PWR sense line"); return; }
        int n = argc > 1 ? atoi(argv[1]) : 10; if (n < 1) n = 1; if (n > 60) n = 60;
        gpio_config_t in = { .pin_bit_mask = 1ULL << sense, .mode = GPIO_MODE_INPUT };
        gpio_config(&in);
        int last = gpio_get_level(sense), boot = gpio_get_level(GPIO_NUM_0), changes = 0; int64_t t0 = esp_timer_get_time(), tl = t0;
        ESP_LOGI(TAG, "pwrpin: GPIO %d reads %d at rest, BOOT (GPIO 0) %d - press either key in the next %d s (nothing sleeps)", sense, last, boot, n);
        while (esp_timer_get_time() - t0 < (int64_t)n * 1000000) {
            int v = gpio_get_level(sense), b = gpio_get_level(GPIO_NUM_0); int64_t now = esp_timer_get_time();
            if (v != last) { ESP_LOGI(TAG, "pwrpin: sense %d -> %d after %d ms", last, v, (int)((now - tl) / 1000)); last = v; tl = now; changes++; }
            if (b != boot) { ESP_LOGI(TAG, "pwrpin: BOOT key %s (t %d ms)", b ? "released" : "PRESSED", (int)((now - t0) / 1000)); boot = b; }
            int k = battery_port_key_poll();             /* the presses it watches are not sleep presses */
            if (k) ESP_LOGI(TAG, "pwrpin: the PMIC saw a %s PWR press (t %d ms)", k == 2 ? "long" : "short", (int)((now - t0) / 1000));
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        while (battery_port_key_poll()) { }
        ESP_LOGI(TAG, "pwrpin: over, %d changes, reads %d", changes, last);
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
        if (!brightness_set_level(atoi(argv[1]))) ESP_LOGW(TAG, "level is 10 .. 100 by tens");
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
    } else if (!strcmp(c, "view")) {                 /* the round board: view fit|full - the frame at 4/5 inside the circle, or px for px with the corners cropped */
        if (!board_is_round()) { ESP_LOGI(TAG, "view: not the round board"); return; }
        if (argc > 1) display_port_set_view(!strcmp(argv[1], "full") ? DISPLAY_VIEW_FULL : DISPLAY_VIEW_FIT);
        ESP_LOGI(TAG, "view: %s", display_port_view() == DISPLAY_VIEW_FULL ? "FULL (px for px, the circle crops the corners)" : "FIT (4/5, the whole frame inside the circle)");
    } else if (!strcmp(c, "touch")) {
        if (argc > 2 && !strcmp(argv[1], "bias")) touch_port_set_bias(atoi(argv[2]));
        if (argc > 2 && !strcmp(argv[1], "log")) { touch_port_set_log(!strcmp(argv[2], "on")); ESP_LOGI(TAG, "touch log %s (a line per press, at its release)", argv[2]); }
        if (argc > 2 && !strcmp(argv[1], "lift")) touch_port_set_lift(atoi(argv[2]), -1);
        if (argc > 2 && !strcmp(argv[1], "said")) touch_port_set_lift(0, !strcmp(argv[2], "on"));
        ESP_LOGI(TAG, "touch bias %d px (reported y - %d)", touch_port_bias(), touch_port_bias());
        if (board_is_round()) ESP_LOGI(TAG, "touch lift: %d ms of silence, a \"no finger\" report lifts at once: %s", touch_port_lift_ms(), touch_port_lift_said() ? "on" : "off");
        int x0, x1, y0, y1; touch_port_raw_seen(&x0, &x1, &y0, &y1);
        if (x1 >= x0) ESP_LOGI(TAG, "touch raw seen since boot: x %d..%d of 0..%d, y %d..%d of 0..%d (scrub the four walls first: the stroke's reach assumes the full span)", x0, x1, TANK_W - 1, y0, y1, TANK_H - 1);
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

/* ---- Improv Wi-Fi over serial (docs/OTA.md, 2026-09-30) ----
 * The web installer (ESP Web Tools) speaks Improv on the same USB port after
 * a flash: it asks the device's state and info, then sends a network and a
 * passphrase. The tank STORES them (net_port_creds_set) and reports
 * "provisioned": the radio never runs beside the tank, so the first CHECK
 * FOR UPDATES is what connects (a wrong passphrase ends on the keyboard
 * there). Packets: "IMPROV" 0x01 <type> <len> <data> <checksum = the sum of
 * every byte before it>. Types: 1 state, 2 error, 3 RPC command, 4 RPC
 * result. RPC: 1 send settings (ssid_len ssid pass_len pass) -> the result
 * carries a URL; 2 request state; 3 request info -> name, version, chip,
 * device name; 4 scan -> an empty list beside the tank (no radio there), the
 * real list in provisioning mode (below). The parser rides the
 * console's byte stream: the six header bytes are matched as they arrive
 * and pulled back out of the command line when they complete. */
/* (2026-10-10: the result used to carry the old site's updates page as the "next URL"; the
   installer page no longer shows a Visit Device link, and the tank hands no URL now) */
/* The handshake's flight recorder (2026-10-04): what the page asked and what the tank answered,
 * across resets (RTC memory, the batlog's pattern), read back with the director's `improv`. The
 * page's connect could not be reproduced from a script - this is the tank's own account of it. */
typedef struct { uint32_t ms; uint8_t kind, a, b, c; } imp_ev_t;   /* kind: 'B' boot (a = reset reason), 'R' ask (a = rpc cmd),
                                                                     'X' dropped (a: 1 stale, 2 checksum, 3 restarted by a header),
                                                                     'T' sent (a = type, b = cmd / state / error), 'F' NOT sent whole, 'H' the poll took over */
#define IMP_EVS 64
RTC_NOINIT_ATTR static imp_ev_t s_ev[IMP_EVS]; RTC_NOINIT_ATTR static uint32_t s_ev_n, s_ev_magic;
static void imp_ev(uint8_t kind, uint8_t a, uint8_t b) {
    if (s_ev_magic != 0x494d5056u || s_ev_n > 1000000u) { s_ev_magic = 0x494d5056u; s_ev_n = 0; }
    imp_ev_t *e = &s_ev[s_ev_n++ % IMP_EVS];
    e->ms = (uint32_t)(esp_timer_get_time() / 1000); e->kind = kind; e->a = a; e->b = b; e->c = 0;
}
static void imp_dump(void) {
    uint32_t n = s_ev_magic == 0x494d5056u ? s_ev_n : 0, from = n > IMP_EVS ? n - IMP_EVS : 0;
    ESP_LOGI(TAG, "improv trace: %lu events (the last %d kept) | B boot(reset reason) R ask(cmd) T sent(type,what) X dropped(1 stale 2 checksum 3 new header) H poll took over", (unsigned long)n, IMP_EVS);
    for (uint32_t i = from; i < n; i++) { const imp_ev_t *e = &s_ev[i % IMP_EVS];
        ESP_LOGI(TAG, "  %7lu ms  %c %d %d", (unsigned long)e->ms, e->kind, e->a, e->b); }
}
#define IMPROV_STALE_US 300000              /* a packet is a few dozen bytes sent at once: 0.3 s without its end = cut short */
static int s_imp_match, s_imp_len, s_imp_n; static bool s_imp_in; static uint8_t s_imp[128];   /* a 32-byte name + a 63-byte passphrase fit */
/* Provisioning mode (2026-10-03, update_mode.c provision_mode_run): right
 * after an install, before the tank exists, the radio is free - the page's
 * scan gets the real list, and the network it sends is CONNECTED to before
 * it is saved, so a wrong password shows on the page, at install. */
static bool s_imp_radio, s_imp_done; static int s_imp_job;   /* job: 0 idle, 1 scanning, 2 connecting */
static bool s_imp_form;                                      /* the page's Wi-Fi form has opened (a scan or a network came) */
static bool s_imp_tank;                                      /* the poll came from the running tank (not update mode) */
static int64_t s_imp_last;                                   /* the last packet from the page (0 = none yet) */
static char s_imp_ssid[NET_SSID_MAX + 1], s_imp_pass[NET_PASS_MAX + 1];
void net_port_esp_connect_budget(int attempts, int wait_ms);
static void improv_send(uint8_t type, const uint8_t *data, int n) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    /* Every packet starts on a NEW LINE and ends one (2026-10-04). The page's parser takes a packet
     * only at the start of a line, and skips everything up to the next newline once a line began
     * with anything else. At a connect Chrome first hands it what sat in the USB FIFO - the tail
     * of some log line, cut off, no newline - so the page was "inside a line" and threw every
     * answer away until the tank's next log line ended (seconds apart on a quiet tank): asked
     * four times, answered four times, not one answer taken. Seen in the bytes Chrome received
     * (a diagnostic page) after the tank's own trace showed it answering every ask. */
    uint8_t p[120]; int k = 0;
    if (n < 0 || n > (int)sizeof p - 12) return;
    p[k++] = '\n';
    memcpy(p + k, "IMPROV", 6); k += 6; p[k++] = 1; p[k++] = type; p[k++] = (uint8_t)n;
    memcpy(p + k, data, n); k += n;
    uint8_t sum = 0; for (int i = 1; i < k; i++) sum += p[i];
    p[k++] = sum;
    p[k++] = '\n';
    int wrote = s_ok ? usb_serial_jtag_write_bytes(p, k, pdMS_TO_TICKS(50)) : -1;
    imp_ev(wrote == k ? 'T' : 'F', type, n ? data[0] : 0);   /* 'F': the driver did not take the whole packet */
#else
    (void)type; (void)data; (void)n;
#endif
}
static void improv_state(void) {
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    uint8_t st = net_port_creds_get(ssid, pass) ? 4 : 2;     /* provisioned : ready */
    improv_send(1, &st, 1);
}
static void improv_result(uint8_t cmd, const char *const *strs, int n) {   /* an RPC result: cmd, len, then length-prefixed strings */
    uint8_t d[96]; int k = 2;
    for (int i = 0; i < n; i++) { int l = (int)strlen(strs[i]); if (k + 1 + l > (int)sizeof d) break; d[k++] = (uint8_t)l; memcpy(d + k, strs[i], l); k += l; }
    d[0] = cmd; d[1] = (uint8_t)(k - 2);
    improv_send(4, d, k);
}
static void improv_handle(void) {
    if (s_imp[7] != 3) return;                               /* only RPC commands come in */
    uint8_t cmd = s_imp[9], n = s_imp[10]; const uint8_t *a = s_imp + 11;
    (void)n;
    s_imp_last = esp_timer_get_time();
    imp_ev('R', cmd, 0);
    if (cmd == 1) {
        int sl = a[0]; const char *sp = (const char *)a + 1; int pl = a[1 + sl]; const char *pp = (const char *)a + 2 + sl;
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        if (sl > NET_SSID_MAX || pl > NET_PASS_MAX || !sl) { uint8_t e = 1; improv_send(2, &e, 1); return; }
        snprintf(ssid, sizeof ssid, "%.*s", sl, sp); snprintf(pass, sizeof pass, "%.*s", pl, pp);
        uint8_t st = 3; improv_send(1, &st, 1);
        if (s_imp_radio) {                                   /* try it first: the answer comes from director_provision_tick */
            s_imp_form = true;
            snprintf(s_imp_ssid, sizeof s_imp_ssid, "%s", ssid); snprintf(s_imp_pass, sizeof s_imp_pass, "%s", pass);
            if (s_imp_job == 1) net_port_abort();
            net_port_esp_connect_budget(2, 15000);           /* inside the page's 45 s */
            net_port_connect_start(s_imp_ssid, s_imp_pass); s_imp_job = 2;
            ESP_LOGI(TAG, "Improv: connecting to %s", ssid);
            return;
        }
        net_port_creds_set(ssid, pass);
        ESP_LOGI(TAG, "Improv: network %s stored (the first CHECK FOR UPDATES connects)", ssid);
        st = 4; improv_send(1, &st, 1);
        improv_result(1, NULL, 0);
        uint8_t e = 0; improv_send(2, &e, 1);
    } else if (cmd == 2) {
        improv_state();
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        if (net_port_creds_get(ssid, pass)) improv_result(2, NULL, 0);
    } else if (cmd == 3) {
        /* the version is the installer manifest's own string (make_installer.py release_version:
           "v0.3.0 alpha (build 1a2b3c4)"): the page hides its update item when the two are equal */
        char ver[48]; snprintf(ver, sizeof ver, "v" PT_RELEASE " " PT_RELEASE_STAGE " (build %s)", version_port_string());
        const char *info[4] = { "Aqua Pets", ver, "esp32-s3", "Aqua Pets" };
        improv_result(3, info, 4);
    } else if (cmd == 4) {
        if (s_imp_radio) s_imp_form = true;
        if (s_imp_radio && s_imp_job == 0) { net_port_scan_start(); s_imp_job = 1; }   /* the list follows from director_provision_tick */
        else if (!s_imp_radio && s_imp_tank) {               /* no radio beside the tank: restart into provisioning mode, which answers this scan */
            ESP_LOGI(TAG, "Improv: the page asked for the networks - saving, restarting into provisioning mode");
            device_provision_request();
        } else if (!s_imp_radio || s_imp_job == 2) improv_result(4, NULL, 0);   /* update mode, or mid-connect: an empty list */
    } else { uint8_t e = 2; improv_send(2, &e, 1); }
}
/* provisioning mode's side of the conversation (update_mode.c) */
void director_provision_begin(void) { s_imp_radio = true; s_imp_done = false; s_imp_job = 0; s_imp_form = false; }
bool director_provision_form(void) { return s_imp_form; }
void director_provision_end(void) { s_imp_radio = false; s_imp_job = 0; }
void director_provision_scan(void) { s_imp_form = true; if (s_imp_radio && s_imp_job == 0) { net_port_scan_start(); s_imp_job = 1; s_imp_last = esp_timer_get_time(); } }
bool director_provision_done(void) { return s_imp_done; }
bool director_provision_busy(void) { return s_imp_job != 0; }
int64_t director_provision_last_us(void) { return s_imp_last; }
void director_provision_tick(void) {
    if (s_imp_job == 1) {
        int st = net_port_scan_state();
        if (st == NET_BUSY) return;
        if (st == NET_DONE) {
            net_ap_t aps[NET_SCAN_MAX]; int n = net_port_scan_results(aps, NET_SCAN_MAX);
            for (int i = 0; i < n; i++) {
                char rssi[8]; snprintf(rssi, sizeof rssi, "%d", aps[i].rssi);
                const char *row[3] = { aps[i].ssid, rssi, aps[i].secured ? "YES" : "NO" };
                improv_result(4, row, 3);
            }
        }
        improv_result(4, NULL, 0);                           /* the end of the list (an empty one after a failed scan) */
        s_imp_job = 0;
        s_imp_last = esp_timer_get_time();                   /* the quiet clock starts when the answer is out */
    } else if (s_imp_job == 2) {
        int st = net_port_connect_state();
        if (st == NET_BUSY) return;
        s_imp_job = 0;
        s_imp_last = esp_timer_get_time();                   /* a refused password: the keeper gets the whole quiet window to type it again */
        if (st == NET_DONE) {
            net_port_creds_set(s_imp_ssid, s_imp_pass);
            ESP_LOGI(TAG, "Improv: connected to %s - stored", s_imp_ssid);
            uint8_t s4 = 4; improv_send(1, &s4, 1);
            improv_result(1, NULL, 0);
            s_imp_done = true;
        } else {
            ESP_LOGW(TAG, "Improv: could not connect to %s (reason %d) - nothing stored", s_imp_ssid, net_port_fail_reason());
            uint8_t e = 3; improv_send(2, &e, 1);            /* "unable to connect": the page asks again */
            uint8_t s2 = 2; improv_send(1, &s2, 1);
        }
        memset(s_imp_pass, 0, sizeof s_imp_pass);
    }
}
/* one byte of the console stream; true = it belonged to an Improv packet */
static bool improv_feed(uint8_t ch) {
    static const char HDR[6] = { 'I', 'M', 'P', 'R', 'O', 'V' };
    /* A packet CUT SHORT must not eat the next one (2026-10-04). The page's connect resets the
     * board and its first ask can arrive with its tail lost in the reset; the old parser then read
     * the next packet's bytes as the rest of the first - a length byte out of the letters, tens of
     * bytes "still to come" - and swallowed every ask that followed: the page saw no tank ("Install
     * Aqua Pets" on a tank that has it, about one connect in three). So: a header seen INSIDE a
     * packet starts a new packet; a packet not finished in IMPROV_STALE_US is dropped; and a packet
     * that fails its checksum is dropped in silence (an error packet makes the page give up at once). */
    static int64_t t_in; static int inner;
    if (s_imp_in && esp_timer_get_time() - t_in > IMPROV_STALE_US) { s_imp_in = false; s_imp_n = 0; s_imp_match = 0; imp_ev('X', 1, 0); }
    if (s_imp_in) {
        inner = ch == HDR[inner] ? inner + 1 : ch == HDR[0] ? 1 : 0;
        if (inner == 6) { inner = 0; s_imp_n = 6; t_in = esp_timer_get_time(); imp_ev('X', 3, 0); return true; }   /* a fresh header: start over from it */
        if (s_imp_n < (int)sizeof s_imp) s_imp[s_imp_n++] = ch;
        if (s_imp_n == 9) s_imp_len = 10 + ch;               /* header 6 + version + type + len byte + data + checksum */
        if (s_imp_n >= 10 && s_imp_n >= s_imp_len) {
            uint8_t sum = 0; for (int i = 0; i < s_imp_len - 1; i++) sum += s_imp[i];
            if (s_imp_len <= (int)sizeof s_imp && sum == s_imp[s_imp_len - 1] && s_imp[6] == 1) improv_handle();
            else imp_ev('X', 2, 0);
            s_imp_in = false; s_imp_n = 0; s_imp_match = 0;
        }
        return true;
    }
    s_imp_match = ch == HDR[s_imp_match] ? s_imp_match + 1 : ch == HDR[0] ? 1 : 0;
    if (s_imp_match == 6) {                                  /* a packet: pull the header back out of the command line */
        s_imp_in = true; s_imp_n = 6; memcpy(s_imp, HDR, 6); t_in = esp_timer_get_time(); inner = 0;
        if (s_len >= 5) s_len -= 5;
        return true;
    }
    return false;
}

void director_init(void) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    if (s_ok) return;                                        /* update mode brought it up already */
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    s_ok = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
    ESP_LOGI(TAG, "%s", s_ok ? "console ready (type help)" : "USB serial driver failed: console off");
    if (nvs_has("bk")) ESP_LOGW(TAG, "a STAGED tank is up: the real tank is parked (restore brings it back)");
    improv_state();                                          /* the installer page listens for this after a flash */
#else
    ESP_LOGI(TAG, "no USB serial: console off");
#endif
}

/* The installer page gives a board it connects to 1.5 s to answer Improv,
 * and its connect RESETS the board: its first ask dies in the bootloader,
 * its second comes 1.0 s in. The console used to come up ~1.3-1.6 s into
 * the boot (after the display, the glass, the PMIC), so a tank with a
 * network saved - which must ANSWER, not just announce itself - was never
 * recognized: the page showed "Install" and no Wi-Fi item. director_early
 * runs at the top of app_main: the console, and a small task that answers
 * Improv (and nothing else) until the first director_poll takes over. */
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
static volatile bool s_early_run; static volatile TaskHandle_t s_early;
static void early_task(void *arg) {
    (void)arg;
    uint8_t buf[32]; int n;
    while (s_early_run) {
        while ((n = usb_serial_jtag_read_bytes(buf, sizeof buf, 0)) > 0)
            for (int i = 0; i < n; i++) improv_feed(buf[i]);   /* a command typed this early is dropped */
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    s_len = 0;
    s_early = NULL;
    vTaskDelete(NULL);
}
#endif
void director_early(void) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    imp_ev('B', (uint8_t)esp_reset_reason(), 0);
    director_init();
    if (!s_ok || s_early) return;
    s_early_run = true;
    TaskHandle_t h = NULL;
    if (xTaskCreate(early_task, "improv", 3072, NULL, 5, &h) == pdPASS) s_early = h; else s_early_run = false;
#endif
}

void director_poll(tank_t *t) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    if (!s_ok) return;
    if (s_early_run) { s_early_run = false; while (s_early) vTaskDelay(1); imp_ev('H', t != NULL, 0); }   /* the boot's Improv task hands over */
    s_imp_tank = t != NULL;
    uint8_t buf[32]; int n;
    while ((n = usb_serial_jtag_read_bytes(buf, sizeof buf, 0)) > 0) {
        for (int i = 0; i < n; i++) {
            char ch = (char)buf[i];
            if (improv_feed(buf[i])) continue;              /* an Improv packet from the installer page */
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
