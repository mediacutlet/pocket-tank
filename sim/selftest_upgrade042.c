#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_upgrade042.c - --selftest-upgrade-042 (0.4.3 release prep, 2026-11-22): THE 0.4.2 SAVE UPGRADE.
 * Whole boards written by the 0.4.2 build itself (main 4dae3468, sim/testdata/upgrade-042: write042.c made them, in the
 * rectangle's world) boot on this build with nothing lost. Each board is everything a sim board keeps - the tank's save
 * and its blobs (.visitors), the shelf's storage (every tank on it and its save), the frame records - and 0.4.2's own
 * read of it (want.txt, upgrade042_dump.inc: every tank in turn, SWAP IN, back).
 *   plain    a new tank, two named fish
 *   full     five named fish, every item 0.4.2 sold, the critters named, the castle LOCKED, the coral SHELVED, two
 *            backgrounds, SELECTIVE BREEDING with a pair set, a day lived, one fish NEXT DOOR
 *   jelly    a crew board: the fish tank on the glass (plant and cluster shelved, volcano locked, 14 visitors collected:
 *            a page and the book), a SUNSET jelly tank and a PIRANHA tank on the shelf
 *   jellyon  the same board with the jelly tank on the glass
 * For every board, on every world:
 *   A  the keeper's read is 0.4.2's, tank by tank: the shelf (ids, types, themes, places, names), the wallet, unlocks,
 *      shelved and locked pieces, the background, critter names, the breeding pair, every fish (name, IDs, parents,
 *      look, stage, next door, meals, badges, trust, FROM, its story's age), the collection (pieces' spots: the
 *      rectangle only - another world lays a rectangle's save out in proportion, as every older fixture)
 *   B  the rectangle only: each tank's save as this build writes it, its first 2200 bytes (all of 0.4.2's save) the
 *      same as 0.4.2's own save of it but for the build's release stamp (1664), the signature mixed from it (owed_check,
 *      2176) and the SWAP IN's fresh carry marker (shelf_mark[2], 1968)
 *   C  the 0.4.3 fields start empty: no paragon part or offer on any fish, no fish stretched, THE FISH LAB not owned, no
 *      octopus (bit 22 off, nothing in 2288..2307), and the castle - owned - at STAGE 1, the starter (none owned: 0)
 * Not covered: two linked boards (the link's live state is never saved; a fish next door is, and is checked), a sand
 * dollar gift in flight, the board's NVS on real flash. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#define UP_CASTLE_STAGE 2280                          /* save_t.castle_stage (progression.c SAVE_AT) */
#define UP_SAVE_MIN 2304                              /* 0.4.3's save: past the octopus's name (2296..2303) */
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "shelf.h"
#include "collection.h"
#include "gift.h"
#include "notice.h"
#include "setup.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
void sim_clock_shift(int64_t s);
void sim_clock_freeze(int on);

static tank_t T;
static gift_book_t G;
static int fails;
#define UF(...) do { printf("FAIL: upgrade-042: "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)

static void boot(void) {                              /* main.c's order, as selftest_move.c */
    render_give_modal_close(); render_reset_close(); render_tap_press_end();
    if (setup_active()) setup_cancel(&T);
    progression_debug_bowl_type(-1);
    shelf_boot(clock_port_now_unix());
    tank_init(&T, 5 + 7919u * shelf_now()->id);     /* (a tank's seed its own: the writer read each tank on the same one) */
    memset(&G, 0, sizeof G); G.persist = true; gift_attach(&T, &G);
    progression_set_jelly(shelf_now()->type == SHELF_TYPE_JELLY);
    progression_boot(&T);
    progression_swap_carry(&T);
    notice_sync(&T);
}
static bool swap_to(int i) {
    static uint16_t thumb[200 * 200]; int w, h; render_shelf_thumb_size(&w, &h);
    for (int k = 0; k < w * h; k++) thumb[k] = (uint16_t)(k * 7);
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    if (!shelf_swap(i, thumb, w, h, &c, sizeof c, clock_port_now_unix())) return false;
    boot();
    return true;
}
#include "upgrade042_dump.inc"

#if !defined(TANK_ROUND) && !defined(TANK_WATCH) && !defined(TANK_SQUARE) && !defined(TANK_P4)   /* (the big screens: a world of their own, their save laid out past 2560 behind its fish tail) */
#define UP_RECT 1                                     /* the fixtures' own world */
#else
#define UP_RECT 0
#endif

/* A: two reads, line by line; a fish's trust and its story's age may move by what a boot lives (a few seconds) */
static bool num_after(const char *line, const char *key, double *v) {
    const char *p = strstr(line, key); if (!p) return false; *v = atof(p + strlen(key)); return true;
}
static void cut_at(char *s, const char *key) { char *p = strstr(s, key); if (p) *p = 0; }
static void compare_reads(const char *board, const char *want, const char *got) {
    FILE *a = fopen(want, "r"), *b = fopen(got, "r");
    if (!a || !b) { UF("%s: no %s", board, a ? got : want); if (a) fclose(a); if (b) fclose(b); return; }
    char la[1024], lb[1024]; int n = 0;
    if (!fgets(la, sizeof la, a) || !fgets(lb, sizeof lb, b)) la[0] = 0;   /* (each read's "now" line) */
    for (;;) {
        char *ra = fgets(la, sizeof la, a), *rb = fgets(lb, sizeof lb, b); n++;
        if (!ra && !rb) break;
        if (!ra || !rb) { UF("%s: line %d: %s", board, n, ra ? "missing on 0.4.3" : "more on 0.4.3 than 0.4.2 read"); break; }
        if (!UP_RECT && !strncmp(la, "pieces ", 7) && !strncmp(lb, "pieces ", 7)) continue;
        double ta, tb, aa, ab;
        bool fish = !strncmp(la, "fish ", 5) && num_after(la, " trust ", &ta) && num_after(lb, " trust ", &tb)
                    && num_after(la, " lived ", &aa) && num_after(lb, " lived ", &ab);
        if (fish) {
            if (fabs(ta - tb) > 0.05) UF("%s: line %d: trust %.3f, 0.4.2 read %.3f", board, n, tb, ta);
            if (fabs(aa - ab) > 300) UF("%s: line %d: its age %.0f s, 0.4.2 read %.0f s", board, n, ab, aa);
            cut_at(la, " trust "); cut_at(lb, " trust ");
        }
        if (strcmp(la, lb)) { la[strcspn(la, "\n")] = 0; lb[strcspn(lb, "\n")] = 0; UF("%s: line %d:\n   0.4.2: %s\n   0.4.3: %s", board, n, la, lb); }
    }
    fclose(a); fclose(b);
}

/* B: the save's stamps that are the clock's (a boot here is later than the writer's) and the build's own */
static bool stamp_byte(size_t off, const uint8_t *was, const uint8_t *now) {
    if (off >= 1664 && off < 1668) return true;       /* saved_release: PT_RELEASE_NUM of the build that wrote it */
    if (off >= 1968 && off < 1972) return true;       /* shelf_mark[SHELF_MARK_CARRY]: a fresh marker at every SWAP IN's carry */
    if (off >= 2176 && off < 2180) return true;       /* owed_check: owed_sig, the head's release and clock mixed (OWED CARDS) */
    (void)was; (void)now;
    return false;
}
static long file_read(const char *p, uint8_t *buf, size_t max) {
    FILE *f = fopen(p, "rb"); if (!f) return -1; long n = (long)fread(buf, 1, max, f); fclose(f); return n;
}
static void compare_saves(const char *board, const char *dir, uint32_t id) {
    static uint8_t a[4096], b[4096]; char pa[900], pb[900];
    snprintf(pa, sizeof pa, "%s/sav-%u.bin", dir, id); snprintf(pb, sizeof pb, "%s/got-%u.bin", dir, id);
    long na = file_read(pa, a, sizeof a), nb = file_read(pb, b, sizeof b);
    if (na != 2200) { UF("%s: tank %u: 0.4.2's save is %ld B (want 2200)", board, id, na); return; }
    if (nb < UP_SAVE_MIN) { UF("%s: tank %u: 0.4.3 saved %ld B (want %d or more)", board, id, nb, UP_SAVE_MIN); return; }
    int shown = 0, diff = 0;
    for (size_t k = 0; k < 2200; k++) if (a[k] != b[k] && !stamp_byte(k, a, b)) {
        diff++; if (shown++ < 8) printf("   %s tank %u: byte %zu was %02x now %02x\n", board, id, k, a[k], b[k]);
    }
    if (diff) UF("%s: tank %u: %d of 0.4.2's 2200 bytes changed", board, id, diff);
    /* C, in the bytes: past 2200 only the castle's stage may be set */
    for (size_t k = 2200; k < (size_t)nb; k++) {
        if (k == UP_CASTLE_STAGE) continue;
        if (b[k]) { UF("%s: tank %u: 0.4.3's byte %zu is %02x on an upgraded save (want 0)", board, id, k, b[k]); break; }
    }
}
/* C, in the tank */
static void check_new_fields(const char *board) {
    for (int i = 0; i < T.n_fish; i++) {
        const fish_t *f = &T.fish[i];
        if (f->evo || f->evo_offer) UF("%s: %s has a paragon part %d / offer %04x after the upgrade", board, f->name, f->evo, f->evo_offer);
        for (int k = 0; k < 2; k++) if (f->body[k]) UF("%s: %s's body is stretched (%d)", board, f->name, f->body[k]);
        for (int k = 0; k < 3; k++) if (f->finl[k]) UF("%s: %s's fins are stretched (%d)", board, f->name, f->finl[k]);
    }
    if (progression_lab_owned(&T)) UF("%s: THE FISH LAB owned after the upgrade", board);
    if (T.sd_unlocks & (SD_ITEM_OCTOPUS | SD_ITEM_LAB)) UF("%s: unlocks %08x carry a 0.4.3 bit", board, T.sd_unlocks);
    int want = (T.sd_unlocks & SD_ITEM_CASTLE) ? 1 : 0;
    if (T.castle_stage != want) UF("%s: the castle's stage %d (want %d: %s)", board, T.castle_stage, want, want ? "the starter" : "none owned");
}

int selftest_upgrade042(void) {
    static const char *const BOARDS[] = { "plain", "full", "jelly", "jellyon" };
    const char *src = "testdata/upgrade-042"; FILE *t = fopen("testdata/upgrade-042/full/want.txt", "r");
    if (!t) src = "sim/testdata/upgrade-042", t = fopen("sim/testdata/upgrade-042/full/want.txt", "r");
    if (!t) { printf("FAIL: upgrade-042: no testdata/upgrade-042 (run from sim/ or the repo root)\n"); return 1; }
    fclose(t);
    int64_t shifted = 0;
    sim_clock_freeze(1);                              /* (one second, as the writer read each board: a second ticking between a tank's
                                                         shelving and its SWAP IN is lived, and moves its clock and its fish) */
    for (size_t bi = 0; bi < sizeof BOARDS / sizeof BOARDS[0]; bi++) {
        const char *board = BOARDS[bi];
        char dir[600], p[900], cmd[2400];
        snprintf(dir, sizeof dir, "%s-upgrade042-%s-%dx%d", sim_shelf_dir(), board, TANK_W, TANK_H);
        snprintf(cmd, sizeof cmd, "rm -rf '%s' && cp -R '%s/%s' '%s' && gunzip -f '%s/storage.bin.gz'", dir, src, board, dir, dir);
        if (system(cmd) != 0) { UF("%s: the fixture's copy", board); continue; }
        snprintf(p, sizeof p, "%s/tank.sav", dir); setenv("POCKET_TANK_SAVE", p, 1);
        snprintf(p, sizeof p, "%s/storage.bin", dir); setenv("POCKET_TANK_STORAGE", p, 1);
        snprintf(p, sizeof p, "%s/frame", dir); setenv("POCKET_TANK_FRAME", p, 1);
        snprintf(p, sizeof p, "%s/wifi.txt", dir); setenv("POCKET_TANK_WIFI", p, 1);
        snprintf(p, sizeof p, "%s/code.txt", dir); setenv("POCKET_TANK_BOARD_CODE_FILE", p, 1);
        unsetenv("POCKET_TANK_BOARD_CODE"); setenv("POCKET_TANK_CREW", "1", 1);
        /* the clock where 0.4.2 left the board: no tank lives the days since the fixture was made */
        long long then = 0; snprintf(p, sizeof p, "%s/want.txt", dir);
        FILE *w = fopen(p, "r"); if (!w || fscanf(w, "now %lld", &then) != 1) { UF("%s: no now in want.txt", board); if (w) fclose(w); continue; }
        fclose(w);
        int64_t d = (int64_t)then - clock_port_now_unix(); sim_clock_shift(d); shifted += d;
        boot();
        const uint32_t glass = shelf_now()->id;
        snprintf(p, sizeof p, "%s/got.txt", dir);
        FILE *o = fopen(p, "w"); if (!o) { UF("%s: no %s", board, p); continue; }
        fprintf(o, "now %lld\n", then);
        uint32_t ids[17]; int n = 0; ids[n++] = glass;
        for (int i = 0; i < shelf_count() && n < 17; i++) ids[n++] = shelf_get(i)->id;
        bool fish_tank_seen = false;
        {   /* the dump's own walk, with C checked in each fish tank on the way */
            up_board_as(o, dir, swap_to, "got");
        }
        fclose(o);
        for (int k = 0; k < n; k++) {                 /* C per tank: swap each in again and look */
            int at = -1; for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == ids[k]) at = i;
            if (shelf_now()->id != ids[k] && (at < 0 || !swap_to(at))) { UF("%s: SWAP IN %u", board, ids[k]); continue; }
            if (shelf_now()->type == SHELF_TYPE_FISH) { check_new_fields(board); fish_tank_seen = true; }
        }
        if (!fish_tank_seen) UF("%s: no fish tank on the board", board);
        char want[900]; snprintf(want, sizeof want, "%s/want.txt", dir);
        compare_reads(board, want, p);
#if UP_RECT
        for (int k = 0; k < n; k++) compare_saves(board, dir, ids[k]);
#endif
        if (!strcmp(board, "jelly") || !strcmp(board, "jellyon")) {   /* the jelly tank: its theme and its 0.4.2 name kept */
            int jt = -1; for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->type == SHELF_TYPE_JELLY) jt = i;
            const shelf_tank_t *j = jt >= 0 ? shelf_get(jt) : shelf_now()->type == SHELF_TYPE_JELLY ? shelf_now() : NULL;
            if (!j || j->variant != 1 || strcmp(j->name, "JELLIES")) UF("%s: the jelly tank %s", board, j ? j->name : "is gone");
        }
        printf("upgrade-042: %s: %d tank%s read as 0.4.2 read %s\n", board, n, n == 1 ? "" : "s", n == 1 ? "it" : "them");
    }
    sim_clock_shift(-shifted); sim_clock_freeze(0);
    if (fails) { printf("selftest-upgrade-042: %d FAILED\n", fails); return 1; }
    printf("selftest-upgrade-042 ok (%s: %s)\n", UP_RECT ? "the fixtures' world" : "another world", UP_RECT ? "reads, saves byte for byte, new fields" : "reads, new fields");
    return 0;
}
