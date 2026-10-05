/* sd_backup.c - copies of the tank on the 2.16's microSD (sd_backup.h).
 *
 * The slot: SDMMC, 1 bit, CLK 2 / CMD 1 / D0 3 (Waveshare's BSP for the
 * board; D3 on GPIO 41 is pulled up and left alone), FAT, no card-detect.
 * Files (8.3 names: long file names are off in this build):
 *   /sdcard/PTANK/SAVE.BIN       the latest copy, replaced by each backup
 *   /sdcard/PTANK/Syymmdd.BIN    one a day (the RTC's date), the last 14 kept
 * A copy is the NVS blob byte for byte - the same thing progression.c reads,
 * whatever build wrote it - written to a temporary file and renamed in, so a
 * cut mid-write never leaves a half SAVE.BIN. */
#include "sd_backup.h"
#include "display_port.h"   /* board_is_sq216 */
#include "progression.h"    /* SAVE_NVS_NS / SAVE_NVS_KEY */
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "sd";
#define MNT       "/sdcard"
#define DIR_      MNT "/PTANK"
#define LATEST    DIR_ "/SAVE.BIN"
#define TMP       DIR_ "/SAVE.TMP"
#define KEEP_DAYS 14
#define SAVE_MAX  8192                          /* far over save_t (~1.7 KB): a newer build's longer save fits */
#define EVERY_US  (3LL * 3600 * 1000000)
#define FIRST_US  (60LL * 1000000)

static sdmmc_card_t *s_card;
static bool s_absent;                           /* the last mount found no card: said once, tried again later */

static bool mount(void) {
    if (!board_is_sq216()) return false;
    const esp_vfs_fat_sdmmc_mount_config_t mc = { .format_if_mount_failed = false, .max_files = 2, .allocation_unit_size = 16 * 1024 };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1; slot.clk = GPIO_NUM_2; slot.cmd = GPIO_NUM_1; slot.d0 = GPIO_NUM_3;
    slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC; slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t e = esp_vfs_fat_sdmmc_mount(MNT, &host, &slot, &mc, &s_card);
    if (e != ESP_OK) {
        if (!s_absent) ESP_LOGI(TAG, "no card (or not FAT): %s - the copies wait for one", esp_err_to_name(e));
        s_absent = true; s_card = NULL; return false;
    }
    if (s_absent) ESP_LOGI(TAG, "a card is in");
    s_absent = false;
    mkdir(DIR_, 0777);
    return true;
}
static void unmount(void) { if (s_card) esp_vfs_fat_sdcard_unmount(MNT, s_card); s_card = NULL; }

static size_t nvs_blob(uint8_t *buf, size_t max) {   /* the save as NVS holds it; 0 = none */
    nvs_handle_t h; size_t len = 0;
    if (nvs_open(SAVE_NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    if (nvs_get_blob(h, SAVE_NVS_KEY, NULL, &len) != ESP_OK || len == 0 || len > max || nvs_get_blob(h, SAVE_NVS_KEY, buf, &len) != ESP_OK) len = 0;
    nvs_close(h);
    return len;
}
static size_t read_file(const char *path, uint8_t *buf, size_t max) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    size_t n = fread(buf, 1, max, f); bool more = fgetc(f) != EOF; fclose(f);
    return more ? 0 : n;                        /* longer than any save: not ours */
}
static bool write_file(const char *path, const uint8_t *buf, size_t len) {
    FILE *f = fopen(TMP, "wb"); if (!f) return false;
    bool ok = fwrite(buf, 1, len, f) == len; ok = fflush(f) == 0 && ok; fclose(f);
    if (!ok) { remove(TMP); return false; }
    remove(path);                               /* FAT's rename does not replace */
    return rename(TMP, path) == 0;
}
static void prune(void) {                       /* the dated copies past KEEP_DAYS go, oldest first (the names sort by date) */
    DIR *d = opendir(DIR_); if (!d) return;
    char names[64][13]; int n = 0; struct dirent *e;
    while ((e = readdir(d)) && n < 64)
        if (e->d_name[0] == 'S' && strlen(e->d_name) == 11 && strcmp(e->d_name, "SAVE.BIN")) { strncpy(names[n], e->d_name, 12); names[n++][12] = 0; }
    closedir(d);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (strcmp(names[i], names[j]) > 0) { char t[13]; memcpy(t, names[i], 13); memcpy(names[i], names[j], 13); memcpy(names[j], t, 13); }
    for (int i = 0; i + KEEP_DAYS < n; i++) { char p[64]; snprintf(p, sizeof p, DIR_ "/%.12s", names[i]); remove(p); }
}

bool sd_backup_now(const char *why) {
    if (!board_is_sq216()) return false;
    uint8_t *buf = malloc(SAVE_MAX); if (!buf) return false;
    size_t len = nvs_blob(buf, SAVE_MAX);
    bool ok = false;
    if (len && mount()) {
        ok = write_file(LATEST, buf, len);
        time_t now = time(NULL);
        if (ok && now > 1700000000) {           /* the RTC's date: one copy a day */
            struct tm tm; localtime_r(&now, &tm);
            char p[40]; snprintf(p, sizeof p, DIR_ "/S%02d%02d%02d.BIN", tm.tm_year % 100, tm.tm_mon + 1, tm.tm_mday);
            ok = write_file(p, buf, len);
            prune();
        }
        unmount();
        ESP_LOGI(TAG, "backup (%s): %u bytes %s", why, (unsigned)len, ok ? "on the card" : "FAILED to write");
    }
    free(buf);
    return ok;
}

static bool restore_from(const char *path) {
    uint8_t *buf = malloc(SAVE_MAX); if (!buf) return false;
    size_t len = read_file(path, buf, SAVE_MAX);
    bool ok = false;
    if (len) {
        nvs_handle_t h;
        if (nvs_open(SAVE_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            ok = nvs_set_blob(h, SAVE_NVS_KEY, buf, len) == ESP_OK && nvs_commit(h) == ESP_OK;
            nvs_close(h);
        }
    }
    ESP_LOGI(TAG, "restore %s: %u bytes %s", path, (unsigned)len, ok ? "-> the save" : "FAILED");
    free(buf);
    return ok;
}
bool sd_backup_restore_if_empty(void) {
    if (!board_is_sq216()) return false;
    nvs_handle_t h; size_t len = 0;
    if (nvs_open(SAVE_NVS_NS, NVS_READONLY, &h) == ESP_OK) { nvs_get_blob(h, SAVE_NVS_KEY, NULL, &len); nvs_close(h); }
    if (len) return false;                      /* a tank is saved: the card never overrides it */
    if (!mount()) return false;
    struct stat st; bool ok = stat(LATEST, &st) == 0 && restore_from(LATEST);
    unmount();
    if (ok) ESP_LOGW(TAG, "no tank in NVS: the card's latest copy brought back");
    return ok;
}
bool sd_backup_restore(const char *name) {
    if (!mount()) return false;
    char p[300]; snprintf(p, sizeof p, DIR_ "/%s", name && *name ? name : "SAVE.BIN");
    bool ok = restore_from(p);
    unmount();
    return ok;
}
void sd_backup_status(void) {
    if (!board_is_sq216()) { ESP_LOGI(TAG, "no SD slot on this board"); return; }
    if (!mount()) return;
    ESP_LOGI(TAG, "card: %s, %llu MB", s_card->cid.name, (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
    DIR *d = opendir(DIR_); struct dirent *e; int n = 0;
    while (d && (e = readdir(d))) {
        char p[300]; struct stat st; snprintf(p, sizeof p, DIR_ "/%s", e->d_name);
        if (stat(p, &st) == 0) { ESP_LOGI(TAG, "  %-12s %6ld bytes", e->d_name, (long)st.st_size); n++; }
    }
    if (d) closedir(d);
    if (!n) ESP_LOGI(TAG, "  (no copies yet)");
    unmount();
}
void sd_backup_poll(int64_t now_us) {
    static int64_t next = FIRST_US;
    if (!board_is_sq216() || now_us < next) return;
    next = now_us + EVERY_US;
    sd_backup_now(now_us < EVERY_US ? "boot" : "every 3 h");
}

/* ---- the SD BACKUPS page (2026-10-04) ---- */
#include "render.h"
#include "lang.h"
#include "tank_events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#define UNDO      DIR_ "/UNDO.BIN"
#define C_INK     0x031015
#define C_INNER   0x1c2f36
#define C_EDGE    0x9fd8e2
#define C_DIM     0x2a3f45
#define C_TEXT    0xffffff
#define C_CAPT    0x9fd8e2
#define C_GO      0x155e58
#define C_GO_E    0x38dcc7
#define SDP_MAX   24
#define SDP_ROWS  5
#define SDP_ROW_Y 76
#define SDP_ROW_DY 44
#define SDP_ROW_H 36
#define SDP_X     32
#define SDP_W     384
#define SDP_FOOT_Y (PAGE_BOWL ? 318 : 312)
#define SDP_CLOSE_X (PAGE_BOWL ? 296 : 324)
#define SDP_ARROW_W 56
#define SDP_YES_X  236
#define SDP_NO_X   72
#define SDP_ASK_Y  244
#define SDP_ASK_W  140
#define SDP_ASK_H  44
typedef struct { char file[13]; int kind; int y, m, d, hh, mm; } sdp_entry_t;   /* kind: 0 latest, 1 before restoring, 2 a day */
static sdp_entry_t s_ent[SDP_MAX];
static int s_n, s_top, s_ask = -1;      /* the entries, the first row shown, the one being confirmed (-1 none) */
static bool s_card_ok;

static int ent_cmp(const void *a, const void *b) {   /* the latest, then "before restoring", then the days newest first */
    const sdp_entry_t *x = a, *y = b;
    if (x->kind != y->kind) return x->kind - y->kind;
    return strcmp(y->file, x->file);
}
void sd_backup_page_open(void) {
    s_n = 0; s_top = 0; s_ask = -1; s_card_ok = mount();
    if (!s_card_ok) return;
    DIR *d = opendir(DIR_); struct dirent *e;
    while (d && (e = readdir(d)) && s_n < SDP_MAX) {
        sdp_entry_t t = { 0 }; int kind = -1;
        if (!strcmp(e->d_name, "SAVE.BIN")) kind = 0;
        else if (!strcmp(e->d_name, "UNDO.BIN")) kind = 1;
        else if (e->d_name[0] == 'S' && strlen(e->d_name) == 11 && sscanf(e->d_name + 1, "%2d%2d%2d", &t.y, &t.m, &t.d) == 3) kind = 2;
        if (kind < 0) continue;
        t.kind = kind; strncpy(t.file, e->d_name, 12);
        if (kind < 2) {                         /* the file's own time (FAT keeps the RTC's at the write) */
            char p[300]; struct stat st; snprintf(p, sizeof p, DIR_ "/%s", e->d_name);
            if (stat(p, &st) == 0 && st.st_mtime > 1700000000) {
                struct tm tm; time_t mt = st.st_mtime; localtime_r(&mt, &tm);
                t.y = tm.tm_year % 100; t.m = tm.tm_mon + 1; t.d = tm.tm_mday; t.hh = tm.tm_hour; t.mm = tm.tm_min;
            } else t.y = -1;
        }
        s_ent[s_n++] = t;
    }
    if (d) closedir(d);
    unmount();
    qsort(s_ent, s_n, sizeof s_ent[0], ent_cmp);
    ESP_LOGI(TAG, "backups page: %d copies", s_n);
}
static void ent_label(const sdp_entry_t *e, char *out, size_t n) {
    char when[24] = "";
    if (e->kind == 2) snprintf(when, sizeof when, "%02d/%02d/%02d", e->d, e->m, e->y);
    else if (e->y >= 0) snprintf(when, sizeof when, "%02d/%02d %02d:%02d", e->d, e->m, e->hh, e->mm);
    if (e->kind == 0) snprintf(out, n, TR("LATEST  %s", "LA ÚLTIMA  %s"), when);
    else if (e->kind == 1) snprintf(out, n, TR("BEFORE RESTORING  %s", "ANTES DE RESTAURAR %s"), when);
    else snprintf(out, n, "%s", when);
}
void sd_backup_page_render(uint16_t *fb, int stride) {
    render_rect(fb, stride, -PAGE_X, -PAGE_Y, TANK_W, TANK_H, C_INK);
    if (s_ask >= 0) {                           /* the question */
        const char *q = TR("GO BACK TO THIS COPY?", "¿VOLVER A ESTA COPIA?");
        render_text(fb, stride, (PAGE_W - render_text_w(q, 3)) / 2, 40, 3, C_TEXT, q);
        char lab[48]; ent_label(&s_ent[s_ask], lab, sizeof lab);
        render_text(fb, stride, (PAGE_W - render_text_w(lab, 2)) / 2, 100, 2, C_CAPT, lab);
        const char *l1 = TR("THE TANK AS IT IS NOW", "LA PECERA DE AHORA SE GUARDA");
        const char *l2 = TR("IS KEPT AS BEFORE RESTORING", "COMO ANTES DE RESTAURAR");
        render_text(fb, stride, (PAGE_W - render_text_w(l1, 2)) / 2, 150, 2, 0x7fa8b0, l1);
        render_text(fb, stride, (PAGE_W - render_text_w(l2, 2)) / 2, 172, 2, 0x7fa8b0, l2);
        render_button(fb, stride, SDP_NO_X, SDP_ASK_Y, SDP_ASK_W, SDP_ASK_H, C_INNER, C_EDGE, "NO", 3);
        render_button(fb, stride, SDP_YES_X, SDP_ASK_Y, SDP_ASK_W, SDP_ASK_H, C_GO, C_GO_E, TR("YES", "SÍ"), 3);
        return;
    }
    const char *t = TR("SD BACKUPS", "COPIAS EN LA SD");
    render_text(fb, stride, (PAGE_W - render_text_w(t, 3)) / 2, 14, 3, C_TEXT, t);
    const char *sub = !s_card_ok ? TR("NO CARD IN THE SLOT", "NO HAY TARJETA") : !s_n ? TR("NO COPIES YET", "AÚN NO HAY COPIAS")
                                 : TR("TAP ONE TO GO BACK TO IT", "TOCA UNA PARA VOLVER A ELLA");
    render_text(fb, stride, (PAGE_W - render_text_w(sub, 2)) / 2, 48, 2, 0x7fa8b0, sub);
    for (int r = 0; r < SDP_ROWS && s_top + r < s_n; r++) {
        char lab[48]; ent_label(&s_ent[s_top + r], lab, sizeof lab);
        render_button(fb, stride, SDP_X, SDP_ROW_Y + r * SDP_ROW_DY, SDP_W, SDP_ROW_H, C_INNER, r + s_top == 0 ? C_EDGE : C_DIM, lab, 2);
    }
    if (s_n > SDP_ROWS) {                       /* the pages of the list */
        render_button(fb, stride, SDP_X, SDP_FOOT_Y, SDP_ARROW_W, 30, C_INNER, s_top > 0 ? C_EDGE : C_DIM, "<", 2);
        render_button(fb, stride, SDP_X + SDP_ARROW_W + 8, SDP_FOOT_Y, SDP_ARROW_W, 30, C_INNER, s_top + SDP_ROWS < s_n ? C_EDGE : C_DIM, ">", 2);
    }
    render_button(fb, stride, SDP_CLOSE_X, SDP_FOOT_Y, 92, 30, C_INNER, C_EDGE, TR("CLOSE", "CERRAR"), 2);
}
static int hit(float x, float y) {              /* 100+i a row, 1 close, 2 yes, 3 no, 4 prev, 5 next */
    x -= PAGE_X; y -= PAGE_Y;
    if (s_ask >= 0) {
        if (y >= SDP_ASK_Y - 10 && y < SDP_ASK_Y + SDP_ASK_H + 14) {
            if (x >= SDP_YES_X - 10 && x < SDP_YES_X + SDP_ASK_W + 10) return 2;
            if (x >= SDP_NO_X - 10 && x < SDP_NO_X + SDP_ASK_W + 10) return 3;
        }
        return 0;
    }
    if (y >= SDP_FOOT_Y - 6) {
        if (x >= SDP_CLOSE_X - 8) return 1;
        if (s_n > SDP_ROWS && x < SDP_X + SDP_ARROW_W + 4) return 4;
        if (s_n > SDP_ROWS && x < SDP_X + 2 * SDP_ARROW_W + 16) return 5;
        return 0;
    }
    for (int r = 0; r < SDP_ROWS && s_top + r < s_n; r++) {
        int ry = SDP_ROW_Y + r * SDP_ROW_DY;
        if (y >= ry - 4 && y < ry + SDP_ROW_H + 4 && x >= SDP_X - 8 && x < SDP_X + SDP_W + 8) return 100 + s_top + r;
    }
    return 0;
}
int sd_backup_page_touch(float x, float y, bool down) {
    static bool s_down; static float s_px, s_py, s_lx, s_ly; static int s_hit;
    int r = SDP_NONE;
    if (down) { s_lx = x; s_ly = y; }
    if (down && !s_down) { s_px = x; s_py = y; s_hit = hit(x, y); }
    else if (!down && s_down) {
        int h = hit(s_lx, s_ly); float dx = s_lx - s_px, dy = s_ly - s_py;
        if (h && h == s_hit && dx * dx + dy * dy < 24 * 24) {
            if (h >= 100) { s_ask = h - 100; tank_emit(TEV_WHEEL_TICK, -1); }
            else if (h == 1) r = SDP_CLOSE;
            else if (h == 2) r = SDP_RESTORE;
            else if (h == 3) s_ask = -1;
            else if (h == 4 && s_top > 0) s_top -= SDP_ROWS;
            else if (h == 5 && s_top + SDP_ROWS < s_n) s_top += SDP_ROWS;
        }
    }
    s_down = down;
    return r;
}
void sd_backup_page_restore(void) {
    if (s_ask < 0 || s_ask >= s_n || !mount()) return;
    uint8_t *buf = malloc(SAVE_MAX);
    size_t len = buf ? nvs_blob(buf, SAVE_MAX) : 0;
    bool kept = len && strcmp(s_ent[s_ask].file, "UNDO.BIN") && write_file(UNDO, buf, len);   /* the tank as it is: one step back */
    free(buf);
    char p[300]; snprintf(p, sizeof p, DIR_ "/%s", s_ent[s_ask].file);
    bool ok = restore_from(p);
    unmount();
    ESP_LOGW(TAG, "backups page: %s restored%s - restarting into it", s_ent[s_ask].file, kept ? " (the tank before it kept as UNDO.BIN)" : "");
    if (ok) { vTaskDelay(pdMS_TO_TICKS(150)); esp_restart(); }
    s_ask = -1;
}
