#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* write042.c - `fishsim --write-042 <out dir>`: real boards written by the 0.4.2 build (main 4dae3468), the fixtures of
 * --selftest-upgrade-042 (0.4.3 release prep). Not part of any release: built only in a scratch checkout of 0.4.2, kept
 * in the repo as sim/testdata/upgrade-042/write042.c so the fixtures can be told apart and made again.
 * Each board is a directory with everything the sim's board keeps: tank.sav (+ .visitors .kin .gifts), storage.bin
 * (the shelf: the tanks on it, their saves), frame/ (the NVS records), and a sidecar want.txt: what 0.4.2 itself reads
 * back after a boot of the same directory (the board as a keeper would see it).
 *   plain  a new tank: the two starter fish named, an hour lived
 *   full   five named fish, every shop item 0.4.2 sells, the critters named, the castle LOCKED, the coral on the shelf,
 *          two backgrounds, SELECTIVE BREEDING with a pair set, a day lived (the story's numbers), one fish next door
 *   jelly  a crew board: a fish tank (the plant and the cluster shelved, the volcano locked) and a jelly tank (SUNSET)
 *          and a PIRANHA tank on the shelf, twelve visitors in the collection plus a book page (14); the fish tank on
 *          the glass
 *   jellyon the jelly board again with the JELLY tank on the glass (its jet and light under 0.4.2's single keys) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static char s_dir[600];
static int fails;
static bool s_fixed;                                  /* finish(): a tank's seed its own, as the 0.4.3 test boots it */
#define WF(...) do { printf("FAIL: write-042: "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)

static void board_env(const char *out, const char *name) {
    char p[800];
    snprintf(s_dir, sizeof s_dir, "%s/%s", out, name);
    char cmd[1400]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
    snprintf(p, sizeof p, "%s/wifi.txt", s_dir); setenv("POCKET_TANK_WIFI", p, 1);
    snprintf(p, sizeof p, "%s/code.txt", s_dir); setenv("POCKET_TANK_BOARD_CODE_FILE", p, 1);
    unsetenv("POCKET_TANK_BOARD_CODE"); setenv("POCKET_TANK_CREW", "1", 1);
}
static void boot(void) {                              /* main.c's order, as selftest_move.c */
    render_give_modal_close(); render_reset_close(); render_tap_press_end();
    if (setup_active()) setup_cancel(&T);
    shelf_boot(clock_port_now_unix());
    static uint32_t boots;
    tank_init(&T, s_fixed ? 5 + 7919u * shelf_now()->id : 5 + 7919u * shelf_now()->id + 104729u * ++boots);   /* (fixed per tank while the board is read: the test's boots are fewer) */
    memset(&G, 0, sizeof G); G.persist = true; gift_attach(&T, &G);
    progression_set_jelly(shelf_now()->type == SHELF_TYPE_JELLY);
    progression_boot(&T);
    progression_swap_carry(&T);
    notice_sync(&T);
}
static void live(float s) { for (int i = 0; i < (int)(s * 25); i++) { tank_tick(&T, 0.04f, advisor_rules); progression_tick(&T, 0.04f); } }
static bool swap_to(int i) {
    static uint16_t thumb[200 * 200]; int w, h; render_shelf_thumb_size(&w, &h);
    for (int k = 0; k < w * h; k++) thumb[k] = (uint16_t)(k * 7);
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    if (!shelf_swap(i, thumb, w, h, &c, sizeof c, clock_port_now_unix())) return false;
    boot();
    return true;
}
static int shelf_of_type(int type) { for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->type == type) return i; return -1; }
static void fish_tank(int n, const char *const *names) {
    progression_setup_done(&T);
    while (T.n_fish < n) tank_add_fish(&T, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    for (int i = 0; i < T.n_fish && names[i]; i++) snprintf(T.fish[i].name, sizeof T.fish[i].name, "%s", names[i]);
}
static void collect(int j) {                          /* a visitor's COLLECT, as selftest_coll.c */
    fish_t *v = &T.fish[N_FISH_MAX]; *v = T.fish[0];
    v->uid = 0x77000000u + (uint32_t)j; v->stage = (stage_t)(1 + j % 3);
    v->color = 0x102030u * (uint32_t)(j % 7 + 1) & 0xffffff; v->shape = (uint8_t)(j % SHAPE_N); v->fins = (uint8_t)(j % FINS_N);
    snprintf(v->name, sizeof v->name, "v%d", j);
    T.vis_used[0] = true; T.vis_badge[0] = (int8_t)(j % 4); T.vis_home[0] = 0x7100 + (uint32_t)j; T.vis_since[0] = T.clock;
    snprintf(T.vis_home_name[0], sizeof T.vis_home_name[0], "%s", j % 5 ? "" : "SAM'S REEF");
    if (!collection_add(&T, 0)) WF("COLLECT %d refused", j);
    T.vis_used[0] = false;
}

/* what a keeper would see (upgrade042_dump.inc: the same text in the 0.4.2 writer and the 0.4.3 test) */
#include "upgrade042_dump.inc"
static void finish(void) {                            /* saved, booted again by 0.4.2, every tank's read and save kept beside the board */
    if (!progression_save(&T)) WF("%s: the save", s_dir);
    sim_clock_freeze(1);                              /* (the board read at one second: a DORMANT tank lives nothing at its SWAP IN) */
    s_fixed = true; boot();
    char p[800]; snprintf(p, sizeof p, "%s/want.txt", s_dir);
    FILE *o = fopen(p, "w"); if (!o) { WF("no %s", p); return; }
    FILE *pre = fopen("/dev/null", "w"); char tmp[700]; snprintf(tmp, sizeof tmp, "%s/frame", s_dir);   /* every tank read once first: a */
    up_board_as(pre, tmp, swap_to, "pre"); fclose(pre);                          /* fresh one is made, the read below is a reload */
    { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -f '%s'/pre-*.bin", tmp); (void)system(cmd); }
    fprintf(o, "now %lld\n", (long long)clock_port_now_unix());
    up_board(o, s_dir, swap_to);
    fclose(o);
    s_fixed = false; sim_clock_freeze(0);
    printf("write-042: %s (%d fish, %d on the shelf, %d SD)\n", s_dir, T.n_fish, shelf_count(), (int)T.sd_balance);
}

int write_042(const char *out) {
    static const char *const FIVE[] = { "otto", "mira", "pip", "lalo", "oodle", NULL };
    /* plain */
    board_env(out, "plain"); boot(); fish_tank(2, FIVE); live(60); sim_clock_shift(3600); boot(); live(5); finish();
    /* full */
    board_env(out, "full"); boot(); fish_tank(5, FIVE);
    progression_sd_grant(&T, 20000);
    for (int k = 0; k < SD_ITEM_COUNT; k++) {
        if (T.sd_unlocks & SD_ITEMS[k].bit) continue;
        bool ok = progression_buy(&T, k); printf("write-042: buy %s %s\n", SD_ITEMS[k].name, ok ? "ok" : "refused");
    }
    if (!progression_bg_buy(&T, 1) || !progression_bg_buy(&T, 2)) WF("backgrounds");
    snprintf(T.crit_name[CRIT_NAME_SNAIL], FISH_NAME_MAX + 1, "turbo"); snprintf(T.crit_name[CRIT_NAME_URCHIN], FISH_NAME_MAX + 1, "spike");
    snprintf(T.crit_name[CRIT_NAME_CRAB], FISH_NAME_MAX + 1, "pinch"); snprintf(T.crit_name[CRIT_NAME_SEAHORSE], FISH_NAME_MAX + 1, "kelpy");
    for (int k = 0; k < SD_ITEM_COUNT; k++) {
        if (SD_ITEMS[k].bit == SD_ITEM_CASTLE && !progression_lock(&T, k, true)) WF("lock the castle");
        if (SD_ITEMS[k].bit == SD_ITEM_CORAL && !progression_shelve(&T, k)) WF("shelve the coral");
    }
    if (!progression_breed_buy(&T)) WF("breeding buy");
    if (!progression_breed_pair(&T, 1, 2)) WF("the pair");
    live(120); sim_clock_shift(86400); boot(); live(30);
    T.fish[4].away = true;                            /* next door when it was saved (save_t.link_away) */
    finish();
    /* jelly, then jellyon */
    for (int on = 0; on < 2; on++) {
        board_env(out, on ? "jellyon" : "jelly"); boot(); fish_tank(3, FIVE);
        progression_sd_grant(&T, 5000);
        for (int k = 0; k < SD_ITEM_COUNT; k++) {
            uint32_t b = SD_ITEMS[k].bit;
            if (b != SD_ITEM_PLANT && b != SD_ITEM_CLUSTER && b != SD_ITEM_VOLCANO && b != SD_ITEM_CRAB) continue;
            if (!(T.sd_unlocks & b) && !progression_buy(&T, k)) WF("buy %s", SD_ITEMS[k].name);
            if ((b == SD_ITEM_PLANT || b == SD_ITEM_CLUSTER) && !progression_shelve(&T, k)) WF("shelve %s", SD_ITEMS[k].name);
            if (b == SD_ITEM_VOLCANO && !progression_lock(&T, k, true)) WF("lock the volcano");
        }
        snprintf(T.crit_name[CRIT_NAME_CRAB], FISH_NAME_MAX + 1, "sid");
        for (int j = 0; j < 14; j++) collect(j);
        live(30); progression_save(&T);
        if (shelf_new_kind(SHELF_TYPE_JELLY, 1, clock_port_now_unix()) < 0) WF("a jelly tank");
        if (shelf_new(SHELF_TYPE_PIRANHA, clock_port_now_unix()) < 0) WF("a piranha tank");
        int j = shelf_of_type(SHELF_TYPE_JELLY);
        if (j < 0 || !swap_to(j)) WF("into the jelly tank");
        live(30);
        if (!on) { int f = shelf_of_type(SHELF_TYPE_FISH); if (f < 0 || !swap_to(f)) WF("back to the fish tank"); live(10); }
        finish();
    }
    printf(fails ? "write-042: %d FAILED\n" : "write-042: ok\n", fails);
    return fails ? 1 : 0;
}
