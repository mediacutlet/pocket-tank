/* persist_port_sim.c — sim implementation of the progression ports:
 * ~/.cache/pocket-tank/tank.sav + wall clock. */
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
    size_t n = fread(buf, 1, max, f); fclose(f);
    if (n == 0) return false;
    *got = n; return true;                       /* a newer build's longer save: its head (see the device port) */
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
bool persist_port_save(const void *buf, size_t len) {
    if (!alive()) return false;
    const char *save = path();
    if (!ensure_parent_dir(save)) return false;
    FILE *f = fopen(save, "wb"); if (!f) return false;
    size_t n = fwrite(buf, 1, len, f); fclose(f); return n == len;
}
bool persist_port_erase(void) { return remove(path()) == 0 || errno == ENOENT; }
#ifndef PT_VERSION
#define PT_VERSION "sim"
#endif
const char *version_port_string(void) { return PT_VERSION; }   /* the Makefile's git describe */
const char *g_sim_hw;                                              /* --hw <id>: the board the sim pretends to run on */
const char *board_port_hw(void) { return g_sim_hw ? g_sim_hw : PT_BOARD; }
static int64_t s_clock_shift;                     /* selftests move the wall clock (time on the shelf) */
void sim_clock_shift(int64_t s) { s_clock_shift += s; }
int64_t clock_port_now_unix(void) { return (int64_t)time(NULL) + s_clock_shift; }

/* ---- the shelf's ports (the tank switcher, common/shelf.h): `storage` as a
 * file (POCKET_TANK_STORAGE, erased = 0xFF like flash) and the NVS namespace
 * `frame` as a directory of key files (POCKET_TANK_FRAME). The selftest cuts
 * the "power" after n writes (shelf_port_sim_cut): every write after it fails,
 * as a board that lost its power would never have made it. ---- */
#include "shelf.h"
#define SIM_SHELF_SIZE (0x2E0000 - 0x10000)       /* the boards' storage, less its first 64 KB */
static int s_cut = -1;                             /* writes left before the cut; -1 = never */
void shelf_port_sim_cut(int writes) { s_cut = writes; }
static bool alive(void) { if (s_cut < 0) return true; if (s_cut == 0) return false; s_cut--; return true; }
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
bool shelf_port_erase(uint32_t off, size_t n) {
    if (!alive()) return false;
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
    size_t n = fread(buf, 1, max, f); fclose(f);
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
bool persist_port_forget(void) { if (!alive()) return false; return remove(path()) == 0 || errno == ENOENT; }
