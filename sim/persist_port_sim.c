/* persist_port_sim.c — sim implementation of the progression ports:
 * ~/.cache/pocket-tank/tank.sav + wall clock. */
#include <stdlib.h>
#include "progression.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/* POCKET_TANK_SAVE overrides the save path (selftests use a scratch file) */
static const char *path(void) {
    static char p[512];
    if (getenv("POCKET_TANK_SAVE")) return getenv("POCKET_TANK_SAVE");
    snprintf(p, sizeof p, "%s/.cache/pocket-tank/tank.sav", getenv("HOME") ? getenv("HOME") : ".");
    return p;
}
bool persist_port_load(void *buf, size_t max, size_t *got) {
    FILE *f = fopen(path(), "rb"); if (!f) return false;
    size_t n = fread(buf, 1, max, f);
    long all = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1; fclose(f);
    if (n == 0) return false;
    *got = all > (long)n ? (size_t)all : n;      /* a newer build's longer save: its head, and the whole length (see the device port) */
    return true;
}

static bool ensure_parent_dir(const char *file) {
    char dir[512]; size_t n = strlen(file);
    if (n >= sizeof dir) return false;
    memcpy(dir, file, n + 1);
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) return true;
    *slash = 0;
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(dir, 0700) != 0 && errno != EEXIST) return false;
        *p = '/';
    }
    return mkdir(dir, 0700) == 0 || errno == EEXIST;
}

static bool alive(void);
static bool s_blocked;                             /* the device port's s_save_blocked (or NVS full): the tank's own keys refuse writes */
void persist_port_sim_block(bool on) { s_blocked = on; }
int g_sim_tank_saves;                             /* the tank's save written, counted (--selftest-dark-notices: one save a tick, T1-F1) */
bool persist_port_save(const void *buf, size_t len) {
    if (s_blocked || !alive()) return false;
    g_sim_tank_saves++;
    const char *save = path();
    if (!ensure_parent_dir(save)) return false;
    FILE *f = fopen(save, "wb"); if (!f) return false;
    size_t n = fwrite(buf, 1, len, f); fclose(f); return n == len;
}
static const char *blob_path(const char *key) { static char p[600]; snprintf(p, sizeof p, "%s.%s", path(), key); return p; }
bool persist_port_erase(void) {                  /* the device's order (persist_port_esp.c): the save first, then the blobs - each one a write
                                                   the selftests' power cut can stop (0.4.1: it was never cut here) */
    static const char *const OWN[3] = { "visitors", "kin", "gifts" };
    if (!alive()) return false;
    if (remove(path()) != 0 && errno != ENOENT) return false;
    for (int k = 0; k < 3; k++) { if (!alive()) return false; remove(blob_path(OWN[k])); }
    return true;
}
bool persist_port_blob_load(const char *key, void *buf, size_t max, size_t *got) {
    FILE *f = fopen(blob_path(key), "rb"); if (!f) return false;
    size_t n = fread(buf, 1, max, f);
    bool more = fgetc(f) != EOF; fclose(f);
    if (!n || more) return false;                  /* (longer than max: refused whole, as NVS does - never a head) */
    *got = n; return true;
}
int g_sim_blob_saves;                             /* blob writes, counted (--selftest-under-card: the KIN book's rate) */
bool persist_port_blob_save(const char *key, const void *buf, size_t len) {
    if (s_blocked || !alive()) return false;
    const char *p = blob_path(key);
    g_sim_blob_saves++;
    if (!ensure_parent_dir(p)) return false;
    FILE *f = fopen(p, "wb"); if (!f) return false;
    size_t n = fwrite(buf, 1, len, f); fclose(f); return n == len;
}
bool persist_port_blob_forget(const char *key) { if (s_blocked || !alive()) return false; return remove(blob_path(key)) == 0 || errno == ENOENT; }
#ifndef PT_VERSION
#define PT_VERSION "sim"
#endif
const char *version_port_string(void) { const char *e = getenv("POCKET_TANK_BUILD_ID"); return e && e[0] ? e : PT_VERSION; }   /* the Makefile's git describe
                                                                  (POCKET_TANK_BUILD_ID: a test's or a picture's own, e.g. an early build's) */
const char *g_sim_hw;                                              /* --hw <id>: the board the sim pretends to run on */
const char *board_port_hw(void) { return g_sim_hw ? g_sim_hw : PT_BOARD; }
static int64_t s_clock_shift;                     /* selftests move the wall clock (time on the shelf) */
void sim_clock_shift(int64_t s) { s_clock_shift += s; }
static int64_t s_clock_frozen;                    /* a selftest that compares a save's bytes across a load holds the wall clock still: a
                                                     second ticking between the save and the load is lived, and moves ages and clocks */
void sim_clock_freeze(int on) { s_clock_frozen = on ? (int64_t)time(NULL) : 0; }
int64_t clock_port_now_unix(void) { return (s_clock_frozen ? s_clock_frozen : (int64_t)time(NULL)) + s_clock_shift; }

/* ---- the shelf's ports (the tank switcher, common/shelf.h): `storage` as a
 * file (POCKET_TANK_STORAGE, erased = 0xFF like flash) and the NVS namespace
 * `frame` as a directory of key files (POCKET_TANK_FRAME). The selftest cuts
 * the "power" after n writes (shelf_port_sim_cut): every write after it fails,
 * as a board that lost its power would never have made it. ---- */
#include "shelf.h"
#define SIM_SHELF_SIZE (0x2E0000 - 0x10000)       /* the boards' storage, less its first 64 KB */
static int s_cut = -1;                             /* writes left before the cut; -1 = never */
static int s_fail = -1;                            /* writes left before ONE that fails (a refused write: the board runs on) */
static int s_fail_n = 1;                           /* ... and how many refused in a row from there (shelf_port_sim_fail_n) */
int shelf_port_sim_cut(int writes) { int was = s_cut; s_cut = writes; return was; }    /* (was: writes still left, 0 = it cut) */
int shelf_port_sim_fail(int writes) { int was = s_fail; s_fail = writes; s_fail_n = 1; return was; }  /* (was -1: it failed, or was off) */
int shelf_port_sim_fail_n(int writes, int n) { int was = s_fail; s_fail = writes; s_fail_n = n > 0 ? n : 1; return was; }   /* n refused in a row */
static int s_refused;                              /* (audit r2 harness: refused writes, counted) */
int shelf_port_sim_refused(void) { int n = s_refused; s_refused = 0; return n; }
static bool alive(void) {
    if (s_fail >= 0) { if (s_fail == 0) { s_refused++; if (--s_fail_n <= 0) s_fail = -1; return false; } s_fail--; }
    if (s_cut < 0) return true; if (s_cut == 0) return false; s_cut--; return true;
}
static const char *store_path(void) {
    static char p[512];
    if (getenv("POCKET_TANK_STORAGE")) return getenv("POCKET_TANK_STORAGE");
    snprintf(p, sizeof p, "%s/.cache/pocket-tank/storage.bin", getenv("HOME") ? getenv("HOME") : ".");
    return p;
}
static FILE *store(void) {
    FILE *f = fopen(store_path(), "r+b");
    if (!f) {                                      /* a new board: the area erased */
        ensure_parent_dir(store_path());
        if (!(f = fopen(store_path(), "w+b"))) return NULL;
        static uint8_t ff[4096]; memset(ff, 0xff, sizeof ff);
        for (size_t o = 0; o < SIM_SHELF_SIZE; o += sizeof ff) fwrite(ff, 1, sizeof ff, f);
    }
    return f;
}
size_t shelf_port_size(void) { return SIM_SHELF_SIZE; }
bool shelf_port_read(uint32_t off, void *buf, size_t n) {
    FILE *f = store(); if (!f) return false;
    bool ok = off + n <= SIM_SHELF_SIZE && !fseek(f, off, SEEK_SET) && fread(buf, 1, n, f) == n;
    fclose(f); return ok;
}
bool shelf_port_write(uint32_t off, const void *buf, size_t n) {
    if (!alive()) return false;
    FILE *f = store(); if (!f) return false;
    bool ok = off + n <= SIM_SHELF_SIZE && !fseek(f, off, SEEK_SET) && fwrite(buf, 1, n, f) == n;
    fclose(f); return ok;
}
int g_sim_shelf_erased;                           /* sectors of the shelf's area erased, counted (--selftest-full-reset: an empty shelf erases none) */
bool shelf_port_erase(uint32_t off, size_t n) {
    if (!alive()) return false;
    g_sim_shelf_erased += (int)(n / 4096);
    if (off % 4096 || n % 4096 || off + n > SIM_SHELF_SIZE) return false;
    FILE *f = store(); if (!f) return false;
    static uint8_t ff[4096]; memset(ff, 0xff, sizeof ff);
    bool ok = !fseek(f, off, SEEK_SET);
    for (size_t o = 0; ok && o < n; o += sizeof ff) ok = fwrite(ff, 1, sizeof ff, f) == sizeof ff;
    fclose(f); return ok;
}
static const char *frame_path(const char *key) {
    static char p[600];
    const char *d = getenv("POCKET_TANK_FRAME");
    if (d) snprintf(p, sizeof p, "%s/%s", d, key);
    else snprintf(p, sizeof p, "%s/.cache/pocket-tank/frame/%s", getenv("HOME") ? getenv("HOME") : ".", key);
    return p;
}
bool shelf_port_frame_get(const char *key, void *buf, size_t max, size_t *got) {
    FILE *f = fopen(frame_path(key), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0 || (size_t)size > max) { fclose(f); return false; }   /* (a record longer than the buffer: refused, as nvs_get_blob's
                                                                         ESP_ERR_NVS_INVALID_LENGTH - never a prefix; round 3 review R2) */
    size_t n = fread(buf, 1, (size_t)size, f); fclose(f);
    *got = n; return n > 0;
}
bool shelf_port_frame_set(const char *key, const void *buf, size_t n) {
    if (!alive()) return false;
    const char *p = frame_path(key);
    if (!ensure_parent_dir(p)) return false;
    char tmp[620]; snprintf(tmp, sizeof tmp, "%s.tmp", p);    /* one key, all or nothing - as NVS writes it */
    FILE *f = fopen(tmp, "wb"); if (!f) return false;
    bool ok = fwrite(buf, 1, n, f) == n; fclose(f);
    return ok && rename(tmp, p) == 0;
}
bool shelf_port_frame_del(const char *key) { if (!alive()) return false; return remove(frame_path(key)) == 0 || errno == ENOENT; }
/* the NVS rescue copy (shelf.h): here a file beside the storage, <storage>.rescue - there is one only if a test makes it */
bool persist_port_rescue_erase(void) {
    char p[600]; snprintf(p, sizeof p, "%s.rescue", store_path());
    FILE *f = fopen(p, "rb"); if (!f) return true;
    fclose(f);
    if (!alive()) return false;
    return remove(p) == 0 || errno == ENOENT;
}
bool persist_port_forget(void) { if (s_blocked || !alive()) return false; return remove(path()) == 0 || errno == ENOENT; }
