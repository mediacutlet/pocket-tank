#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_home.c - --selftest-home (0.4.2): A HARD BOOT LOADS THE MAIN TANK. Strato 11-11: "a hard reboot of the device
 * should always load the default main aquarium tank, as an additional safe guard to get out of experimental tanks if they
 * freeze or boot button fails to launch the tank switcher". What counts as a hard boot is the board's (main.c app_main, the
 * reset reason: not testable here); this is the rest - which tank comes back (shelf_home_note / shelf_home_pick) and the
 * swap main.c hard_boot_home makes with it, in main.c's order.
 *   A  a fish tank on the glass is noted as the main tank at its boot; an experimental tank's boot leaves the note alone
 *   B  the pick is the fish tank LAST ON THE GLASS, not the one at the lowest place
 *   C  the hard boot's swap: the main tank on the glass, the experimental tank whole on the shelf (its type, its theme, its
 *      name), the wallet carried over; and the experimental tank swaps back in after
 *   D  the main tank removed while the experimental tank is up: the fish tank at the lowest place
 *   E  no fish tank left on the board: a new one, free (the wallet untouched), and it boots as a new tank does
 *   F  ERASE EVERYTHING forgets the note
 *   G  the icon foot's places, and THE LIBRARY's BACK whole
 *   H  AN EXPERIMENTAL TANK, BOUGHT ONCE (the piranha tank, the minnows): 500 the first time and the tank made, both or
 *      neither; one a kind; removed, it sells for nothing and comes back free; a short wallet buys nothing; a tank's
 *      own boot (RESET keeps the frame) keeps the record, ERASE EVERYTHING clears it; a kind found on the board at a
 *      boot is paid for; the bowl's fish may not go into one */
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

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
int64_t clock_port_now_unix(void);

static tank_t T;
static gift_book_t G;
static int fails;
#define HM_FAIL(...) do { printf("FAIL: home: "); printf(__VA_ARGS__); printf("\n"); if (++fails > 14) return 1; } while (0)

static char s_dir[400];
static void board_env(void) {
    char p[600];
    snprintf(s_dir, sizeof s_dir, "%s-home-%dx%d", sim_shelf_dir(), TANK_W, TANK_H);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
    snprintf(p, sizeof p, "%s/wifi.txt", s_dir); setenv("POCKET_TANK_WIFI", p, 1);
    snprintf(p, sizeof p, "%s/code.txt", s_dir); setenv("POCKET_TANK_BOARD_CODE_FILE", p, 1);
    unsetenv("POCKET_TANK_BOARD_CODE"); setenv("POCKET_TANK_CREW", "1", 1);
}
static void board_new(void) { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd); }
static void boot(void) {                              /* main.c's order: the shelf, the tank, a swap's carry, the main tank's note */
    if (setup_active()) setup_cancel(&T);
    shelf_boot(clock_port_now_unix());
    static uint32_t boots;
    tank_init(&T, 11 + 7919u * shelf_now()->id + 104729u * ++boots);
    memset(&G, 0, sizeof G); G.persist = true; gift_attach(&T, &G);
    progression_set_jelly(SHELF_TYPE_EXPERIMENTAL(shelf_now()->type));   /* (main.c: any experimental tank's save is an empty tank) */
    progression_boot(&T);
    progression_swap_carry(&T);
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
static bool hard_boot(void) {                         /* main.c hard_boot_home, after the experimental tank's save has loaded */
    boot();
    if (shelf_now()->type == SHELF_TYPE_FISH) return false;
    int pick = shelf_home_pick(clock_port_now_unix());
    return pick >= 0 && swap_to(pick);
}
static uint32_t home_id(void) { uint32_t id = 0; size_t got = 0; return shelf_port_frame_get("home", &id, sizeof id, &got) && got == sizeof id ? id : 0; }
static int index_of(uint32_t id) { for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->id == id) return i; return -1; }

int selftest_home(void) {
    board_env(); board_new();
    static uint16_t scene[TANK_W * TANK_H];
    render_set_scene_cache(scene);
    boot();
    const uint32_t t1 = shelf_now()->id;
    /* A */
    if (shelf_now()->type != SHELF_TYPE_FISH || home_id() != t1) HM_FAIL("A. a new board: the main tank's note is %u, the tank on the glass %u", (unsigned)home_id(), (unsigned)t1);
    /* B: a second fish tank at a higher place becomes the one last on the glass */
    int k2 = shelf_new(SHELF_TYPE_FISH, clock_port_now_unix());
    if (k2 < 0) { HM_FAIL("B. staging: a second fish tank"); return 1; }
    const uint32_t t2 = shelf_get(k2)->id; const int pos2 = shelf_get(k2)->pos;
    if (!swap_to(k2)) { HM_FAIL("B. staging: the swap to the second fish tank"); return 1; }
    if (shelf_now()->id != t2 || home_id() != t2) HM_FAIL("B. the second fish tank on the glass: the note is %u (want %u)", (unsigned)home_id(), (unsigned)t2);
    int kj = shelf_new_kind(SHELF_TYPE_JELLY, 2, clock_port_now_unix());
    if (kj < 0) { HM_FAIL("B. staging: a jelly tank"); return 1; }
    const uint32_t tj = shelf_get(kj)->id;
    T.sd_balance = 777; T.sd_earned = 900;
    if (!swap_to(kj)) { HM_FAIL("B. staging: the swap to the jelly tank"); return 1; }
    if (shelf_now()->type != SHELF_TYPE_JELLY || home_id() != t2) HM_FAIL("A. the jelly tank's boot moved the note: %u (want %u)", (unsigned)home_id(), (unsigned)t2);
    {   int pick = shelf_home_pick(clock_port_now_unix());
        if (pick < 0 || shelf_get(pick)->id != t2) HM_FAIL("B. the pick is tank %u, the tank last on the glass was %u (at place %d; tank %u is at a lower one)",
                                                           pick < 0 ? 0u : (unsigned)shelf_get(pick)->id, (unsigned)t2, pos2, (unsigned)t1); }
    /* C */
    const int n0 = shelf_count();
    if (!hard_boot()) { HM_FAIL("C. the hard boot's swap was refused"); return 1; }
    if (shelf_now()->id != t2 || shelf_now()->type != SHELF_TYPE_FISH) HM_FAIL("C. after the hard boot tank %u (type %d) is on the glass, want %u", (unsigned)shelf_now()->id, shelf_now()->type, (unsigned)t2);
    if (T.sd_balance != 777) HM_FAIL("C. the wallet did not come over: %d (want 777)", (int)T.sd_balance);
    if (T.n_fish < 2) HM_FAIL("C. the main tank came back with %d fish", T.n_fish);
    {   int j = index_of(tj);
        if (j < 0 || shelf_count() != n0) HM_FAIL("C. the jelly tank is not on the shelf (%d tanks there, were %d)", shelf_count(), n0);
        else if (shelf_get(j)->type != SHELF_TYPE_JELLY || shelf_get(j)->variant != 2 || strcmp(shelf_get(j)->name, "JELLIES"))
            HM_FAIL("C. the jelly tank on the shelf: type %d theme %d name %s", shelf_get(j)->type, shelf_get(j)->variant, shelf_get(j)->name);
        else if (!swap_to(j) || shelf_now()->id != tj || T.n_fish != 0 || T.sd_balance != 777) HM_FAIL("C. the jelly tank does not swap back in whole (%d fish, wallet %d)", T.n_fish, (int)T.sd_balance);
    }
    if (hard_boot()) { if (shelf_now()->id != t2) HM_FAIL("C. a second hard boot brought tank %u", (unsigned)shelf_now()->id); }
    else HM_FAIL("C. a second hard boot's swap was refused");
    boot();
    if (hard_boot()) HM_FAIL("C. a hard boot with a fish tank on the glass swapped something");
    if (shelf_now()->id != t2) HM_FAIL("C. ... and tank %u is on the glass", (unsigned)shelf_now()->id);
    /* D: the jelly tank up, the main tank removed */
    if (!swap_to(index_of(tj))) { HM_FAIL("D. staging: the jelly tank back on the glass"); return 1; }
    if (!shelf_remove(index_of(t2))) { HM_FAIL("D. staging: the main tank removed"); return 1; }
    {   int pick = shelf_home_pick(clock_port_now_unix());
        if (pick < 0 || shelf_get(pick)->id != t1) HM_FAIL("D. the main tank gone: the pick is %u, want the fish tank left (%u)", pick < 0 ? 0u : (unsigned)shelf_get(pick)->id, (unsigned)t1); }
    /* E: none left */
    if (!shelf_remove(index_of(t1)) || shelf_count() != 0) { HM_FAIL("E. staging: the last fish tank removed"); return 1; }
    if (!hard_boot()) { HM_FAIL("E. no fish tank on the board: the hard boot's swap was refused"); return 1; }
    if (shelf_now()->type != SHELF_TYPE_FISH || shelf_now()->id == tj) HM_FAIL("E. no fish tank on the board: type %d is on the glass", shelf_now()->type);
    if (T.sd_balance != 777) HM_FAIL("E. the new main tank cost sand dollars: wallet %d (want 777)", (int)T.sd_balance);
    if (T.n_fish != 2) HM_FAIL("E. the new main tank has %d fish (a new tank: two fry)", T.n_fish);
    if (index_of(tj) < 0) HM_FAIL("E. the jelly tank is not on the shelf");
    if (home_id() != shelf_now()->id) HM_FAIL("E. the new main tank is not noted");
    /* F */
    if (!shelf_erase_all()) HM_FAIL("F. ERASE EVERYTHING refused");
    if (home_id() != 0) HM_FAIL("F. ERASE EVERYTHING kept the main tank's note (%u)", (unsigned)home_id());
    /* G: the icon foot left THE LIBRARY's BACK its whole button (it borrowed MY TANKS's CLOSE until 0.4.2), and each of
       the foot's four icons and both arrows has a place of its own */
    {   int back = 0, hit[SHELF_TAP_SLOT0] = { 0 };
        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
            back += render_library_tap((float)x, (float)y, 2) == LIB_TAP_BACK;
            int r = render_shelf_tap((float)x, (float)y); if (r > 0 && r < SHELF_TAP_SLOT0) hit[r]++;
        }
        if (back < (MSP_CLOSE_W + 8) * (MSP_CLOSE_H + 8)) HM_FAIL("G. THE LIBRARY's BACK answers on %d px (its button alone is %d)", back, MSP_CLOSE_W * MSP_CLOSE_H);
        static const int want[] = { SHELF_TAP_CLOSE, SHELF_TAP_LIBRARY, SHELF_TAP_PREV, SHELF_TAP_NEXT, SHELF_TAP_TOUCH_LOCK, SHELF_TAP_SETTINGS };
        for (unsigned k = 0; k < sizeof want / sizeof want[0]; k++) if (hit[want[k]] < 44 * 30) HM_FAIL("G. MY TANKS's foot: tap %d answers on %d px (a button is %d)", want[k], hit[want[k]], 44 * 30);
    }
    /* H */
    board_new(); boot();
    for (int ty = SHELF_TYPE_PIRANHA; ty <= SHELF_TYPE_MINNOWS; ty++) {
        const char *nm = ty == SHELF_TYPE_PIRANHA ? "PIRANHA" : "MINNOWS";
        T.sd_balance = 499; progression_save(&T);
        if (shelf_xt_price(ty) != SHELF_XT_PRICE || shelf_type_paid(ty)) HM_FAIL("H. %s on a new board: price %d, paid %d", nm, shelf_xt_price(ty), shelf_type_paid(ty));
        if (progression_get_xt(&T, ty, clock_port_now_unix()) >= 0 || T.sd_balance != 499 || shelf_count_type(ty)) HM_FAIL("H. %s with 499: bought (wallet %d, %d tanks)", nm, (int)T.sd_balance, shelf_count_type(ty));
        T.sd_balance = 1200; progression_save(&T);
        int i = progression_get_xt(&T, ty, clock_port_now_unix());
        if (i < 0 || T.sd_balance != 700 || shelf_count_type(ty) != 1 || !shelf_type_paid(ty)) { HM_FAIL("H. %s bought: index %d, wallet %d (want 700), %d tanks, paid %d", nm, i, (int)T.sd_balance, shelf_count_type(ty), shelf_type_paid(ty)); continue; }
        if (strcmp(shelf_get(i)->name, nm) || shelf_get(i)->type != ty) HM_FAIL("H. the new tank is %s, type %d", shelf_get(i)->name, shelf_get(i)->type);
        if (shelf_xt_price(ty) != -1 || progression_get_xt(&T, ty, clock_port_now_unix()) >= 0 || shelf_count_type(ty) != 1 || T.sd_balance != 700) HM_FAIL("H. a second %s was made (or paid for)", nm);
        boot();
        if (T.sd_balance != 700) HM_FAIL("H. %s: the price did not land in the save (wallet %d after a boot)", nm, (int)T.sd_balance);
        i = -1; for (int k = 0; k < shelf_count(); k++) if (shelf_get(k)->type == ty) i = k;
        if (i < 0) { HM_FAIL("H. %s is gone after a boot", nm); continue; }
        if (shelf_refund_value(i, NULL) != 0) HM_FAIL("H. %s sells back for %d (want 0)", nm, shelf_refund_value(i, NULL));
        if (progression_remove_tank(&T, i) != 0 || shelf_count_type(ty) != 0 || T.sd_balance != 700) HM_FAIL("H. REMOVE %s: %d tanks left, wallet %d", nm, shelf_count_type(ty), (int)T.sd_balance);
        if (!shelf_type_paid(ty) || shelf_xt_price(ty) != 0) HM_FAIL("H. %s removed: paid %d, price %d (want paid, 0)", nm, shelf_type_paid(ty), shelf_xt_price(ty));
        T.sd_balance = 3; progression_save(&T);
        i = progression_get_xt(&T, ty, clock_port_now_unix());
        if (i < 0 || T.sd_balance != 3 || shelf_count_type(ty) != 1) HM_FAIL("H. %s got again: index %d, wallet %d (want 3: free)", nm, i, (int)T.sd_balance);
        if (i >= 0) {                                     /* on the glass: an empty tank, nothing moves in from the bowl */
            if (!swap_to(i) || shelf_now()->type != ty || T.n_fish != 0) HM_FAIL("H. %s on the glass: type %d, %d fish", nm, shelf_now()->type, T.n_fish);
            if (!hard_boot() || shelf_now()->type != SHELF_TYPE_FISH) HM_FAIL("H. a hard boot with %s on the glass left type %d there", nm, shelf_now()->type);
            int k = -1; for (int q = 0; q < shelf_count(); q++) if (shelf_get(q)->type == ty) k = q;
            if (k < 0 || !shelf_remove(k)) HM_FAIL("H. staging: %s removed again", nm);
        }
    }
    {   uint8_t none = 0; shelf_port_frame_set("xbuy", &none, 1);          /* the record lost (a cut before it was written), a tank of the kind on the board */
        if (shelf_new_kind(SHELF_TYPE_MINNOWS, 0, clock_port_now_unix()) < 0) HM_FAIL("H. staging: a minnows tank by hand");
        boot(); shelf_paid_note();
        if (!shelf_type_paid(SHELF_TYPE_MINNOWS) || shelf_type_paid(SHELF_TYPE_PIRANHA)) HM_FAIL("H. the boot's note: minnows paid %d (want 1), piranha paid %d (want 0)", shelf_type_paid(SHELF_TYPE_MINNOWS), shelf_type_paid(SHELF_TYPE_PIRANHA));
        shelf_type_paid_set(SHELF_TYPE_PIRANHA);
        if (!shelf_erase_all()) HM_FAIL("H. ERASE EVERYTHING refused");
        if (shelf_type_paid(SHELF_TYPE_MINNOWS) || shelf_type_paid(SHELF_TYPE_PIRANHA)) HM_FAIL("H. ERASE EVERYTHING kept what was paid for");
    }
    if (!fails) printf("selftest-home: PASS (%dx%d)\n", TANK_W, TANK_H);
    return fails != 0;
}
