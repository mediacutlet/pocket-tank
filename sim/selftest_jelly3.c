#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_jelly3.c - --selftest-jelly3 (0.4.3, docs/release-0.4.3.md item 1): UP TO THREE JELLY TANKS, ONE A THEME. Strato to
 * the crew, 11-14: "For now you can install one jelly tank per board. In 0.4.3 that goes up to three." The shelf's side
 * (shelf_new_jelly, shelf_jelly_themes) and the boot's, in main.c's order (selftest_home.c's), on a board of its own:
 *   A  each theme installs once, by its theme's name; a second of a theme, or a theme out of range, is refused and uses
 *      nothing (no slot, no place, no shelf ID's tank); all three present
 *   B  REMOVE one: it sells for nothing, its theme is free, it installs again
 *   C  MY TANKS: each at its own place; SWAP IN each in turn - the right theme on the glass, an empty save, the wallet carried
 *   D  a restart keeps all three, and the one on the glass comes back on it
 *   E  STORAGE FULL: the shelves filled with fish tanks - a jelly tank refused cleanly (no tank made, none lost, the boot finds
 *      every tank) - and one place freed, it installs
 *   F  A BOARD FROM BEFORE 0.4.3: one jelly tank made as 0.4.2 made it (shelf_new_kind: JELLIES, theme SUNSET) - its header
 *      read back byte for byte after the update's boot and after two more themes installed beside it; it swaps in as it was;
 *      its own theme is refused, the other two install. An older build (a rollback) is NOT run here: no record's format
 *      changed, so its loader reads the same slots; what is checked is this build's boot finding every tank, the fish tanks
 *      first of all
 *   (not here: main.c's per-theme jet/light keys and their migration - NVS, the board's only)
 *   G  THE THANK-YOU (xt_notice.h): the first jelly tank says it; a second theme, a third, one installed again after a REMOVE
 *      never do; PIRANHAS still says it; a board that saw it on a jelly tank before the jelly mark existed does not see it again
 *   H  PICK A THEME's controls with themes installed: the three columns, NEW TANK and BACK where they are drawn; nothing
 *      installed - the page and its taps as 0.4.2's (selftest_libload compares the pixels) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tank.h"
#include "render.h"
#include "progression.h"
#include "shelf.h"
#include "notice.h"
#include "gift.h"
#include "setup.h"
#include "jelly.h"
#include "xt_notice.h"

const char *sim_shelf_dir(void);
int64_t clock_port_now_unix(void);

static tank_t T;
static gift_book_t G;
static int fails;
#define J3_FAIL(...) do { printf("FAIL: jelly3: "); printf(__VA_ARGS__); printf("\n"); if (++fails > 14) return 1; } while (0)

static char s_dir[400];
static void board_env(void) {
    char p[600];
    snprintf(s_dir, sizeof s_dir, "%s-jelly3-%dx%d", sim_shelf_dir(), TANK_W, TANK_H);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
    snprintf(p, sizeof p, "%s/wifi.txt", s_dir); setenv("POCKET_TANK_WIFI", p, 1);
    snprintf(p, sizeof p, "%s/code.txt", s_dir); setenv("POCKET_TANK_BOARD_CODE_FILE", p, 1);
    unsetenv("POCKET_TANK_BOARD_CODE"); setenv("POCKET_TANK_CREW", "1", 1);
}
static void board_new(void) { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd); }
static void boot(void) {                              /* main.c's order (selftest_home.c) */
    if (setup_active()) setup_cancel(&T);
    shelf_boot(clock_port_now_unix());
    static uint32_t boots;
    tank_init(&T, 13 + 7919u * shelf_now()->id + 104729u * ++boots);
    memset(&G, 0, sizeof G); G.persist = true; gift_attach(&T, &G);
    progression_set_jelly(SHELF_TYPE_EXPERIMENTAL(shelf_now()->type));
    progression_boot(&T);
    progression_shelf_settle(&T);
    progression_swap_carry(&T);
    shelf_paid_note();
    shelf_home_note();
    notice_sync(&T);
    if (setup_active()) setup_cancel(&T);
}
static bool swap_to(int i) {
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    if (!shelf_swap(i, NULL, 0, 0, &c, sizeof c, clock_port_now_unix())) return false;
    boot();
    return true;
}
static int index_of(uint32_t id) { for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == id) return i; return -1; }
static int jelly_of(int theme) { for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->type == SHELF_TYPE_JELLY && shelf_get(i)->variant == theme) return i; return -1; }
static int tanks(void) { return shelf_count() + 1; }
static int fish_tanks(void) { return shelf_count_type(SHELF_TYPE_FISH); }
static int install(int theme) { return shelf_new_jelly(theme, theme >= 0 && theme < JELLY_THEME_N ? JELLY_TANK_NAMES[theme] : "X", clock_port_now_unix()); }
static bool notice_shows(void) {                      /* the bare tank's frames until it comes up (xt_notice.h: 1.5 s), then a tap closes it */
    for (int f = 0; f < 100; f++) { xt_notice_tick(0.04f, false); if (xt_notice_up()) { xt_notice_tick(0.04f, false); xt_notice_tap(0); return true; } }
    return false;
}
static int seen_count(void) { struct { uint32_t magic, n, id[XT_SEEN_MAX]; } r; size_t got = 0; return shelf_port_frame_get(XT_SEEN_KEY, &r, sizeof r, &got) && got >= 8 ? (int)r.n : 0; }

int selftest_jelly3(void) {
    board_env();
    /* ---- A ---- */
    board_new(); boot();
    while (T.n_fish < 4) tank_add_fish(&T, 0, 1);
    T.sd_balance = 640; progression_save(&T);
    if (shelf_jelly_themes() != 0) J3_FAIL("A. a new board has jelly themes %x", shelf_jelly_themes());
    const int free0 = shelf_free_places();
    uint32_t jid[3] = { 0 };
    for (int k = 0; k < JELLY_THEME_N; k++) {
        int i = install(k);
        if (i < 0) { J3_FAIL("A. %s refused on a board without it", JELLY_THEMES[k].name); continue; }
        const shelf_tank_t *h = shelf_get(i);
        if (h->type != SHELF_TYPE_JELLY || h->variant != k || strcmp(h->name, JELLY_TANK_NAMES[k]) || h->save_len)
            J3_FAIL("A. %s made as type %d theme %d '%s' (save %d)", JELLY_THEMES[k].name, h->type, h->variant, h->name, h->save_len);
        jid[k] = h->id;
        if (shelf_jelly_themes() != (1u << (k + 1)) - 1) J3_FAIL("A. after %s the themes read %x", JELLY_THEMES[k].name, shelf_jelly_themes());
        const int n = tanks(), fp = shelf_free_places();
        if (install(k) >= 0 || tanks() != n || shelf_free_places() != fp) J3_FAIL("A. a second %s was made (%d tanks, %d places)", JELLY_THEMES[k].name, tanks(), shelf_free_places());
    }
    if (install(-1) >= 0 || install(3) >= 0 || install(200) >= 0) J3_FAIL("A. a theme out of range was made");
    if (shelf_count_type(SHELF_TYPE_JELLY) != 3 || shelf_jelly_themes() != 7 || shelf_free_places() != free0 - 3)
        J3_FAIL("A. all three: %d jelly tanks, themes %x, %d places (want %d)", shelf_count_type(SHELF_TYPE_JELLY), shelf_jelly_themes(), shelf_free_places(), free0 - 3);
    if (T.sd_balance != 640) J3_FAIL("A. the wallet moved: %d", (int)T.sd_balance);
    /* ---- B ---- */
    {   int i = jelly_of(1);
        if (i < 0 || shelf_refund_value(i, NULL) != 0) J3_FAIL("B. SUNSET would sell for %d", i < 0 ? -1 : shelf_refund_value(i, NULL));
        if (i >= 0 && progression_remove_tank(&T, i) != 0) J3_FAIL("B. REMOVE SUNSET refused or paid");
        if (shelf_jelly_themes() != 5 || T.sd_balance != 640) J3_FAIL("B. after REMOVE: themes %x, wallet %d", shelf_jelly_themes(), (int)T.sd_balance);
        boot();
        if (shelf_jelly_themes() != 5 || index_of(jid[1]) >= 0) J3_FAIL("B. a restart brought SUNSET back (themes %x)", shelf_jelly_themes());
        i = install(1);
        if (i < 0 || shelf_get(i)->variant != 1 || strcmp(shelf_get(i)->name, JELLY_TANK_NAMES[1]) || shelf_jelly_themes() != 7) J3_FAIL("B. SUNSET installed again: %d, themes %x", i, shelf_jelly_themes());
        if (i >= 0) jid[1] = shelf_get(i)->id;
    }
    /* ---- C, D ---- */
    {   int pos[3];
        for (int k = 0; k < 3; k++) { int i = index_of(jid[k]); pos[k] = i >= 0 ? shelf_get(i)->pos : -1; if (i < 0 || shelf_at_pos(pos[k]) != i) J3_FAIL("C. %s not at its place", JELLY_THEMES[k].name); }
        if (pos[0] == pos[1] || pos[1] == pos[2] || pos[0] == pos[2]) J3_FAIL("C. two jelly tanks share a place");
        const uint32_t fish_id = shelf_now()->id;
        for (int k = 2; k >= 0; k--) {                /* DEEP, SUNSET, MOONLIGHT: each from the one before (a jelly tank to a jelly tank) */
            int i = index_of(jid[k]);
            if (i < 0 || !swap_to(i)) { J3_FAIL("C. SWAP IN %s failed", JELLY_THEMES[k].name); continue; }
            if (shelf_now()->id != jid[k] || shelf_now()->type != SHELF_TYPE_JELLY || shelf_now()->variant != k || strcmp(shelf_now()->name, JELLY_TANK_NAMES[k]))
                J3_FAIL("C. on the glass: id %u theme %d '%s' (want %s)", (unsigned)shelf_now()->id, shelf_now()->variant, shelf_now()->name, JELLY_THEMES[k].name);
            if (T.n_fish != 0 || T.sd_balance != 640) J3_FAIL("C. %s came up with %d fish, wallet %d", JELLY_THEMES[k].name, T.n_fish, (int)T.sd_balance);
            if (shelf_jelly_themes() != 7) J3_FAIL("C. with %s on the glass the themes read %x", JELLY_THEMES[k].name, shelf_jelly_themes());
            if (k == 1) {                             /* ---- D: a restart on SUNSET ---- */
                progression_save(&T); boot();
                if (shelf_now()->id != jid[1] || shelf_now()->variant != 1 || shelf_jelly_themes() != 7 || shelf_count_type(SHELF_TYPE_JELLY) != 3)
                    J3_FAIL("D. a restart: on the glass %u theme %d, themes %x, %d jelly tanks", (unsigned)shelf_now()->id, shelf_now()->variant, shelf_jelly_themes(), shelf_count_type(SHELF_TYPE_JELLY));
            }
        }
        int f = index_of(fish_id);
        if (f < 0 || !swap_to(f) || shelf_now()->id != fish_id || T.n_fish != 4 || T.sd_balance != 640)
            J3_FAIL("C. back to the fish tank: %d fish, wallet %d", T.n_fish, (int)T.sd_balance);
        boot();
        if (shelf_jelly_themes() != 7 || T.n_fish != 4) J3_FAIL("D. a restart on the fish tank: themes %x, %d fish", shelf_jelly_themes(), T.n_fish);
    }
    /* ---- E ---- */
    {   int i = jelly_of(2); if (i >= 0) progression_remove_tank(&T, i);   /* DEEP's place, for the full board */
        int made = 0;
        while (shelf_free_places() > 0 && made < SHELF_SLOTS_MAX) { if (shelf_new(SHELF_TYPE_FISH, clock_port_now_unix()) < 0) break; made++; }
        const int n = tanks(), nf = fish_tanks();
        if (shelf_free_places() != 0) J3_FAIL("E. the shelves did not fill (%d places left after %d tanks)", shelf_free_places(), made);
        if (install(2) >= 0 || tanks() != n || shelf_jelly_themes() != 3) J3_FAIL("E. a full board made DEEP (%d tanks, themes %x)", tanks(), shelf_jelly_themes());
        boot();
        if (tanks() != n || fish_tanks() != nf || shelf_jelly_themes() != 3 || T.n_fish != 4) J3_FAIL("E. after the refusal and a restart: %d tanks (%d), %d fish tanks (%d), themes %x", tanks(), n, fish_tanks(), nf, shelf_jelly_themes());
        printf("selftest-jelly3: E. this board's storage: %d tanks (%d on the shelves + the glass), %d places free - the slot a swap needs kept\n", tanks(), shelf_count(), shelf_free_places());
        for (int k = 0; k < shelf_count(); k++) if (shelf_get(k)->type == SHELF_TYPE_FISH && !shelf_get(k)->save_len) { progression_remove_tank(&T, k); break; }   /* (a fresh one) */
        if (shelf_free_places() != 1 || install(2) < 0 || shelf_jelly_themes() != 7) J3_FAIL("E. one place freed: DEEP not made (themes %x)", shelf_jelly_themes());
    }
    /* ---- F: a board from before 0.4.3 ---- */
    board_new(); boot();
    while (T.n_fish < 3) tank_add_fish(&T, 0, 1);
    T.sd_balance = 210; progression_save(&T);
    {   int i = shelf_new_kind(SHELF_TYPE_JELLY, 1, clock_port_now_unix());   /* (0.4.2's own call: JELLIES, theme SUNSET) */
        if (i < 0) { J3_FAIL("F. no 0.4.2 jelly tank"); goto G_; }
        if (shelf_new(SHELF_TYPE_FISH, clock_port_now_unix()) < 0) J3_FAIL("F. a second fish tank");
        const shelf_tank_t was = *shelf_get(i);
        if (strcmp(was.name, "JELLIES")) J3_FAIL("F. 0.4.2's jelly tank is named '%s'", was.name);
        boot();                                       /* the update's boot */
        i = index_of(was.id);
        if (i < 0 || memcmp(shelf_get(i), &was, sizeof was)) J3_FAIL("F. the old jelly tank's header changed at the update's boot");
        if (shelf_jelly_themes() != 2 || install(1) >= 0) J3_FAIL("F. its theme was offered again (themes %x)", shelf_jelly_themes());
        if (install(0) < 0 || install(2) < 0 || shelf_jelly_themes() != 7) J3_FAIL("F. the other two themes beside it: themes %x", shelf_jelly_themes());
        boot();
        i = index_of(was.id);
        if (i < 0 || memcmp(shelf_get(i), &was, sizeof was)) J3_FAIL("F. the old jelly tank's header changed with two more beside it");
        if (fish_tanks() != 2 || T.n_fish != 3 || T.sd_balance != 210) J3_FAIL("F. the fish tanks: %d (2), %d fish, wallet %d", fish_tanks(), T.n_fish, (int)T.sd_balance);
        if (i >= 0 && swap_to(i)) {
            if (shelf_now()->id != was.id || shelf_now()->variant != 1 || strcmp(shelf_now()->name, "JELLIES") || T.n_fish != 0 || T.sd_balance != 210)
                J3_FAIL("F. the old jelly tank swapped in as theme %d '%s', %d fish, wallet %d", shelf_now()->variant, shelf_now()->name, T.n_fish, (int)T.sd_balance);
        } else J3_FAIL("F. the old jelly tank would not swap in");
        int f = -1; for (int k = 0; k < shelf_count() && f < 0; k++) if (shelf_get(k)->type == SHELF_TYPE_FISH && shelf_get(k)->save_len) f = k;
        if (f < 0 || !swap_to(f) || T.n_fish != 3) J3_FAIL("F. back to the fish tank: %d fish", T.n_fish);
    }
G_:
    /* ---- G: the thank-you ---- */
    board_new(); boot();
    {   int ia = install(0), ib = install(1);
        if (ia < 0 || ib < 0) { J3_FAIL("G. MOONLIGHT and SUNSET not installed"); goto H_; }
        uint32_t a = shelf_get(ia)->id, b = shelf_get(ib)->id;
        int pi = shelf_new_kind(SHELF_TYPE_PIRANHA, 0, clock_port_now_unix());
        uint32_t p = pi >= 0 ? shelf_get(pi)->id : 0;
        xt_notice_open_jelly(a);
        if (!notice_shows()) J3_FAIL("G. the first jelly tank did not say thank you");
        xt_notice_open_jelly(a); if (notice_shows()) J3_FAIL("G. the first jelly tank said it twice");
        xt_notice_open_jelly(b); if (notice_shows()) J3_FAIL("G. a second jelly theme said it again");
        if (!xt_notice_seen(XT_SEEN_JELLY)) J3_FAIL("G. the jelly mark is not in the seen list");
        progression_remove_tank(&T, index_of(a));
        int ia2 = install(0); if (ia2 < 0) { J3_FAIL("G. MOONLIGHT not installed again"); goto H_; }
        uint32_t a2 = shelf_get(ia2)->id;
        xt_notice_open_jelly(a2); if (notice_shows()) J3_FAIL("G. MOONLIGHT installed again said it again");
        int ic = install(2); if (ic < 0) { J3_FAIL("G. DEEP not installed"); goto H_; }
        uint32_t c = shelf_get(ic)->id;
        xt_notice_open_jelly(c); if (notice_shows()) J3_FAIL("G. the third theme said it again");
        xt_notice_open(p, true); if (!notice_shows()) J3_FAIL("G. PIRANHAS did not say thank you");
        if (seen_count() != 3) J3_FAIL("G. the seen list holds %d (want 3: the first jelly tank, the mark, the piranhas)", seen_count());
        /* a board that saw it before the mark: the list holds the jelly tank's id alone */
        struct { uint32_t magic, n, id[1]; } old = { XT_SEEN_MAGIC, 1, { b } };
        shelf_port_frame_set(XT_SEEN_KEY, &old, sizeof old);
        xt_notice_open_jelly(c); if (notice_shows()) J3_FAIL("G. a board that saw it on a jelly tank before the mark saw it again");
        if (!xt_notice_seen(XT_SEEN_JELLY)) J3_FAIL("G. the old board's match did not write the mark (a REMOVE of that tank would bring it back)");
        for (uint32_t k = 0; k < 2 * XT_SEEN_MAX; k++) { xt_notice_open(5000 + k, true); notice_shows(); }   /* a full list, turned over twice */
        if (!xt_notice_seen(XT_SEEN_JELLY) || !xt_notice_seen(5000 + 2 * XT_SEEN_MAX - 1)) J3_FAIL("G. a full seen list let the jelly mark go");
        xt_notice_open_jelly(a2); if (notice_shows()) J3_FAIL("G. after a full list a jelly tank said it again");
        xt_notice_open(0, false);
    }
H_:
    /* ---- H: PICK A THEME's controls with themes installed ---- */
    {   static uint16_t fb[TANK_W * TANK_H];
        const char *names[3] = { "MOONLIGHT", "SUNSET", "DEEP" }; const uint16_t *th[3] = { NULL, NULL, NULL };
        int x, y, w, h, nb = 0, bb = 0, col = 0;
        render_jelly_pick_set_have(5);
        render_jelly_pick(fb, TANK_W, names, th, 1);
        for (int k = 0; k < 3; k++) {
            render_jelly_pick_pic_rect(k, &x, &y, &w, &h);
            if (render_jelly_pick_tap(x + w / 2, y + h / 2) == JP_TAP_THEME0 + k) col++;
            if (render_jelly_pick_tap(x + w / 2, y + h + 34) == JP_TAP_THEME0 + k) col++;   /* (its name) */
        }
        {   int low_have = 0, low_none = 0;                 /* the column's tap band reaches down over the INSTALLED line (18 px more) */
            render_jelly_pick_pic_rect(0, &x, &y, &w, &h);
            for (int yy = y; yy < TANK_H; yy++) if (render_jelly_pick_tap(x + w / 2, yy) == JP_TAP_THEME0) low_have = yy;
            int y_have = y;
            render_jelly_pick_set_have(0); render_jelly_pick_pic_rect(0, &x, &y, &w, &h);
            for (int yy = y; yy < TANK_H; yy++) if (render_jelly_pick_tap(x + w / 2, yy) == JP_TAP_THEME0) low_none = yy;
            if (low_have - y_have != low_none - y + 18) J3_FAIL("H. the column's band ends %d px under its picture with themes installed, %d without (want 18 more)", low_have - y_have, low_none - y);
            render_jelly_pick_set_have(5);
        }
        for (int yy = TANK_H / 2; yy < TANK_H; yy += 2) for (int xx = 0; xx < TANK_W; xx += 2) { int r = render_jelly_pick_tap(xx, yy); nb += r == JP_TAP_NEW; bb += r == JP_TAP_BACK; }
        if (col != 6) J3_FAIL("H. the theme columns answer %d of 6 taps (pictures and their words)", col);
        if (!nb || !bb) J3_FAIL("H. NEW TANK (%d) or BACK (%d) has no place", nb, bb);
        render_jelly_pick_set_have(0);
    }
    if (fails) return 1;
    printf("selftest-jelly3: each theme once by its name, a second refused, REMOVE frees it, MY TANKS swaps to each, a restart keeps all three, a full board refuses cleanly, a 0.4.2 jelly tank loads unchanged, one thank-you for all jelly tanks: PASS\n");
    return 0;
}
