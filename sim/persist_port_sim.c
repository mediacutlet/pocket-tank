/* persist_port_sim.c — sim implementation of the progression ports:
 * ~/.cache/aqua-pets/tank.sav + wall clock. */
#include "progression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/* AQUA_PETS_SAVE overrides the save path (selftests use a scratch file) */
static const char *path(void) {
    static char p[512];
    if (getenv("AQUA_PETS_SAVE")) return getenv("AQUA_PETS_SAVE");
    snprintf(p, sizeof p, "%s/.cache/aqua-pets/tank.sav", getenv("HOME") ? getenv("HOME") : ".");
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

bool persist_port_save(const void *buf, size_t len) {
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
int64_t clock_port_now_unix(void) { return (int64_t)time(NULL); }
