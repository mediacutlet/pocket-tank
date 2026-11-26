#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_move.c - --selftest-move (0.4.2 item 14, docs/move-fish-0.4.2.md): THE TRAVEL BOWL.
 * A fish moved between the tanks of one board: MOVE on its card puts it in the board's travel bowl, the keeper swaps
 * tanks in MY TANKS as ever, LET IN lets it into the tank on the glass. One fish; time does not pass in the bowl; no sand
 * dollars either way; never two of the fish and never none.
 *   A  a whole move, tank A -> the bowl -> SWAP IN -> tank B, two days on the clock between: every field of the fish as it
 *      left (name, IDs, look, genes, stage, drives, temper, meals, badges, age, lived time, the ledger, FROM); its
 *      parents named in B (the KIN book taught); no gift note, no visitor collected, the wallet as it was; B's residents
 *      lived the two days, the bowl's fish none of them
 *   B  ... and back again to A: the same fish, once, its parents' slots found again
 *   C  THE POWER CUT at every write of MOVE (out), then at every write of the boot after, then a clean boot: the fish
 *      in the tank or in the bowl - one, never two, never none. The same for LET IN
 *   D  one REFUSED write at every step of MOVE and of LET IN, the board running on (a save, a swap away and back, a boot):
 *      one fish; the first write refused = nothing happened, said in a plain line
 *   E  every refusal and its line: the last two, a fry not yet named (lit and dark), the bowl taken, next door, at a
 *      door, born next door, being given, a visitor; LET IN: the tank full (five fish; four and a place held), the wrong
 *      habitat (another kind of creature, the jelly tank, a tank type this build does not know); a fish whose record
 *      stands is not sold or given
 *   F  the bowl is the board's: through a REMOVE, a RESET THIS TANK and a NEW TANK it holds its fish; ERASE EVERYTHING
 *      empties it (and after a boot)
 *   G  nothing to farm: twenty trips there and back - the wallet, what was earned and both tanks' badges the same
 *   H  the IS WAITING card: by itself once a start, never in the dark (it comes with the light), never over a page, after
 *      the queued notices; NOT NOW - not again by itself; the keeper page's waiting row brings it back; not right after MOVE
 *   I  this glass: the card's buttons (RENAME | GIVE | MOVE | SELL | MORE) each its own place inside the card and the
 *      glass, a tap on each does its own thing; the cards' buttons; every line fits its card with the longest name
 * The power cut on the real flash (NVS's own journal) and the room in the board's NVS are not modelled here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "shelf.h"
#include "collection.h"
#include "gift.h"
#include "notice.h"
#include "passport.h"
#include "touch_gate.h"
#include "setup.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
int  shelf_port_sim_cut(int writes);
int  shelf_port_sim_fail(int writes);
void sim_clock_shift(int64_t s);

static tank_t T;
static gift_book_t G;
static uint16_t fa[TANK_W * TANK_H];
static int fails;
#define MV_FAIL(...) do { printf("FAIL: move: "); printf(__VA_ARGS__); printf("\n"); if (++fails > 14) return 1; } while (0)

/* ---- the board: its save, its storage and its frame in one scratch directory ---- */
static char s_dir[400], s_pre[420];
static void board_env(void) {
    char p[600];
    snprintf(s_dir, sizeof s_dir, "%s-move-%dx%d", sim_shelf_dir(), TANK_W, TANK_H);
    snprintf(s_pre, sizeof s_pre, "%s.pre", s_dir);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
    snprintf(p, sizeof p, "%s/wifi.txt", s_dir); setenv("POCKET_TANK_WIFI", p, 1);
    snprintf(p, sizeof p, "%s/code.txt", s_dir); setenv("POCKET_TANK_BOARD_CODE_FILE", p, 1);
    unsetenv("POCKET_TANK_BOARD_CODE"); setenv("POCKET_TANK_CREW", "1", 1);
}
static void board_new(void) { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd); }
static void dir_copy(const char *from, const char *to) { char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && cp -R '%s' '%s'", to, from, to); (void)system(cmd); }
static void boot(void) {                              /* main.c's order: the shelf, the tank (a jelly tank said first), a swap's carry */
    render_give_modal_close(); render_reset_close(); render_tap_press_end();
    if (setup_active()) setup_cancel(&T);
    progression_debug_bowl_type(-1);
    shelf_boot(clock_port_now_unix());
    static uint32_t boots;
    tank_init(&T, 5 + 7919u * shelf_now()->id + 104729u * ++boots);   /* (a seed a boot, as a board's clock gives: a fresh tank's IDs are its own) */
    memset(&G, 0, sizeof G); G.persist = true; gift_attach(&T, &G);   /* (the tank's own notes: on its flash, with it on the shelf) */
    progression_set_jelly(shelf_now()->type == SHELF_TYPE_JELLY);
    progression_boot(&T);
    progression_swap_carry(&T);
    notice_sync(&T);
}
static void live(float s) { for (int i = 0; i < (int)(s * 25); i++) { tank_tick(&T, 0.04f, advisor_rules); progression_tick(&T, 0.04f); } }
static bool has_frame(const char *key) { uint8_t b[512]; size_t got = 0; return shelf_port_frame_get(key, b, sizeof b, &got); }
static bool swap_to(int i) {                          /* MY TANKS' SWAP IN, as main.c: only from a save that landed */
    if (!progression_save(&T)) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    if (!shelf_swap(i, NULL, 0, 0, &c, sizeof c, clock_port_now_unix())) return false;
    boot();
    return true;
}
static int find(uint32_t uid) { for (int i = 0; i < T.n_fish; i++) if (T.fish[i].uid == uid) return i; return -1; }
static int here(uint32_t uid) { int n = 0; for (int i = 0; i < T.n_fish; i++) n += T.fish[i].uid == uid; return n; }
static int bowl(uint32_t uid) { return progression_bowl_has() && progression_bowl_uid() == uid; }
/* the fish on the whole board: in the tank on the glass, in the other tank (swapped in to look, then back), in the bowl */
static int copies(uint32_t uid, int *a, int *b, int *w) {
    *a = here(uid); *w = bowl(uid); *b = 0;
    if (shelf_count() > 0) { if (!swap_to(0)) return -1; *b = here(uid); *w |= bowl(uid); if (!swap_to(0)) return -1; }
    return *a + *b + *w;
}

/* ---- every field of a fish that is its own ---- */
typedef struct {
    char name[FISH_NAME_MAX + 1]; uint32_t uid, pu[2], color, fin, accent; int preset, stage;
    uint8_t shape, fins, pattern, carried[GENE_N], mutated, from_b, from_carried;
    float hunger, energy, stress, curiosity, bored, trust, sociable, bold, lazy, bold0, sociable0, drift_acc, size;
    int32_t eaten, eaten_player; uint32_t ms_bits, ms_seen, paid, from; float age; double lived;
} snap_t;
static void snap(int i, snap_t *s) {
    const fish_t *f = &T.fish[i]; memset(s, 0, sizeof *s);
    memcpy(s->name, f->name, sizeof s->name); s->uid = f->uid; s->pu[0] = f->parent_uid[0]; s->pu[1] = f->parent_uid[1];
    s->color = f->color; s->fin = f->fin; s->accent = f->accent; s->preset = f->preset; s->stage = f->stage;
    s->shape = f->shape; s->fins = f->fins; s->pattern = f->pattern; memcpy(s->carried, f->carried, GENE_N);
    s->mutated = f->mutated; s->from_b = f->from_b; s->from_carried = f->from_carried;
    s->hunger = f->hunger; s->energy = f->energy; s->stress = f->stress; s->curiosity = f->curiosity; s->bored = f->bored; s->trust = f->trust;
    s->sociable = f->sociable; s->bold = f->bold; s->lazy = f->lazy; s->bold0 = f->bold0; s->sociable0 = f->sociable0;
    s->drift_acc = f->drift_acc; s->size = f->size; s->eaten = f->eaten; s->eaten_player = f->eaten_player;
    s->ms_bits = f->ms_bits; s->ms_seen = f->ms_seen; s->paid = T.sd_paid_fish[i];
    s->from = progression_fish_from(&T, f); s->age = progression_age_s(&T, i); s->lived = progression_lived_s(&T, i);
}
static const char *snap_diff(const snap_t *a, const snap_t *b) {   /* NULL = the same fish in every field */
#define SD(f, what) if (memcmp(&a->f, &b->f, sizeof a->f)) return what
    SD(name, "its name"); SD(uid, "its ID"); SD(pu, "its parents' IDs"); SD(color, "its body color"); SD(fin, "its fin color"); SD(accent, "its accent");
    SD(preset, "its preset"); SD(stage, "its stage"); SD(shape, "its shape gene"); SD(fins, "its fins gene"); SD(pattern, "its pattern gene");
    SD(carried, "the genes it carries"); SD(mutated, "its mutation"); SD(from_b, "its genes' sources"); SD(from_carried, "its carried sources");
    SD(hunger, "its hunger"); SD(energy, "its energy"); SD(stress, "its stress"); SD(curiosity, "its curiosity"); SD(bored, "its boredom"); SD(trust, "its trust");
    SD(sociable, "its sociable"); SD(bold, "its bold"); SD(lazy, "its lazy"); SD(bold0, "its birth bold"); SD(sociable0, "its birth sociable");
    SD(drift_acc, "its drift"); SD(size, "its size"); SD(eaten, "its meals"); SD(eaten_player, "its meals by hand");
    SD(ms_bits, "its badges"); SD(ms_seen, "its seen badges"); SD(paid, "its sand dollar ledger (paid marks)"); SD(age, "its age"); SD(lived, "its lived time");
#undef SD
    return NULL;
}

/* ---- a board with two tanks: A (three fish: two founders and PIP, their grown fry, every payout taken) and B (a pair) ---- */
static uint32_t PIP, MA, PA;
static bool two_tanks(void) {
    board_new(); boot(); progression_setup_done(&T);
    while (T.n_fish < 3) tank_add_fish(&T, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    snprintf(T.fish[0].name, sizeof T.fish[0].name, "ma"); snprintf(T.fish[1].name, sizeof T.fish[1].name, "pa"); snprintf(T.fish[2].name, sizeof T.fish[2].name, "pip");
    MA = T.fish[0].uid; PA = T.fish[1].uid; PIP = T.fish[2].uid;
    for (int i = 0; i < 3; i++) progression_set_age(&T, i, STAGE_ADULT_AGE + 500.0f + i * 77);
    fish_t *f = &T.fish[2];
    f->trust = 10.0f; f->hunger = 3.2112f; f->energy = 6.4321f; f->stress = 1.2345f; f->curiosity = 4.5678f; f->bored = 2.3456f;
    f->bold = 0.61234f; f->sociable = 0.37654f; f->drift_acc = 12.3456f; f->eaten = 41; f->eaten_player = 17;
    T.sd_balance = 1000;
    live(2.0f);                                       /* every payout owed is taken (stages, trust): the ledgers are full */
    if (!progression_save(&T)) return false;
    if (shelf_new(SHELF_TYPE_FISH, clock_port_now_unix()) < 0) return false;
    if (!swap_to(0)) return false;                    /* B: a fresh pair, welcomed, saved */
    progression_setup_done(&T); live(1.0f);
    if (!progression_save(&T)) return false;
    if (!swap_to(0)) return false;                    /* A on the glass again */
    return find(PIP) == 2 && T.n_fish == 3 && shelf_count() == 1 && !progression_bowl_has() && !has_frame("move");
}
/* the card's own taps: its step's hold lived out, then the button */
static int card_tap(int which) {
    live(TAP_W_S + 0.2f);
    render_frame_begin(); render_tank(&T, fa, TANK_W); render_give_modal(&T, fa, TANK_W);
    int x = 0, y = 0;
    if (!render_give_modal_button(&T, which, &x, &y)) return -1;
    int r = render_give_modal_tap(&T, (float)x, (float)y);
    live(0.1f);                                       /* (progression_tick does what was asked) */
    render_give_modal_step(&T);
    return r;
}
static bool on_glass(int x, int y) {
#ifdef TANK_ROUND
    float dx = x - TANK_W / 2.0f, dy = y - TANK_H / 2.0f; return dx * dx + dy * dy <= (TANK_W / 2.0f - 4) * (TANK_W / 2.0f - 4);
#else
    return x >= 4 && x < TANK_W - 4 && y >= 4 && y < TANK_H - 4;
#endif
}

int selftest_move(void) {
    board_env();
    static uint16_t scene[TANK_W * TANK_H];
    render_set_scene_cache(scene);

    /* ---- A + B: there, two days later, and back ---- */
    if (!two_tanks()) { MV_FAIL("staging the two tanks"); return 1; }
    snap_t was, now; snap(2, &was);
    const int32_t bal0 = T.sd_balance, earned0 = T.sd_earned; const int coll0 = collection_count();
    const uint32_t A_uid = T.uid;
    if (was.paid != (SD_PAID_JUV | SD_PAID_ADULT | SD_PAID_TRUST)) MV_FAIL("A. staging: PIP's ledger is %x", (unsigned)was.paid);
    if (progression_move_why(&T, 2) != BOWL_OK) MV_FAIL("A. PIP may not go into the bowl (%d)", progression_move_why(&T, 2));
    if (progression_move_out(&T, 2) != BOWL_OK) MV_FAIL("A. MOVE refused");
    if (here(PIP) || !bowl(PIP) || T.n_fish != 2 || !has_frame("move")) MV_FAIL("A. after MOVE: %d in the tank, bowl %d, %d fish", here(PIP), bowl(PIP), T.n_fish);
    if (strcasecmp(progression_bowl_name(), "pip")) MV_FAIL("A. the bowl's fish is named \"%s\"", progression_bowl_name());
    live(1.0f);
    if (T.sd_balance != bal0 || T.sd_earned != earned0) MV_FAIL("A. MOVE moved sand dollars: %d (was %d)", (int)T.sd_balance, (int)bal0);
    if (!swap_to(0)) { MV_FAIL("A. the swap to B"); return 1; }
    snap_t res0; snap(0, &res0);                      /* (a resident of B, before the two days) */
    if (!swap_to(0)) { MV_FAIL("A. the swap back"); return 1; }
    sim_clock_shift(2 * 86400);                       /* two days on the board's clock: B, DORMANT on its shelf, lives them */
    if (!swap_to(0)) { MV_FAIL("A. the swap to B, two days on"); return 1; }
    snap_t res1; snap(0, &res1);
    if (!(res1.lived > res0.lived + 3600)) MV_FAIL("A. B's resident did not live the two days (%.0f -> %.0f s): the frozen-time check proves nothing", res0.lived, res1.lived);
    if (here(PIP) || !bowl(PIP)) MV_FAIL("A. in B before LET IN: %d in the tank, bowl %d", here(PIP), bowl(PIP));
    live(2.0f);                                       /* (what B's own fish earned in their two days is paid first) */
    const uint32_t B_tms0 = T.tank_ms_bits; const int32_t balB = T.sd_balance, earnedB = T.sd_earned;
    if (progression_bowl_why(&T) != BOWL_OK) MV_FAIL("A. B would not take PIP (%d)", progression_bowl_why(&T));
    if (progression_bowl_let_in(&T) != BOWL_OK) MV_FAIL("A. LET IN refused");
    if (here(PIP) != 1 || bowl(PIP) || T.n_fish != 3 || has_frame("move")) MV_FAIL("A. after LET IN: %d in the tank, bowl %d, %d fish, record %d", here(PIP), bowl(PIP), T.n_fish, has_frame("move"));
    if (find(PIP) >= 0) {
        snap(find(PIP), &now);
        const char *d = snap_diff(&was, &now);
        if (d) MV_FAIL("A. PIP arrived in B changed: %s (two days in the bowl must cost it nothing)", d);
        if (now.from != A_uid) MV_FAIL("A. PIP's FROM in B is %08x, not tank A's %08x", (unsigned)now.from, (unsigned)A_uid);
        const kin_t *k0 = kin_find(now.pu[0]), *k1 = kin_find(now.pu[1]);
        if (!k0 || !k1 || strcasecmp(k0->name, "ma") || strcasecmp(k1->name, "pa")) MV_FAIL("A. PIP's parents are not named in B (FAMILY would show \"?\"): %s / %s", k0 ? k0->name : "?", k1 ? k1->name : "?");
    }
    if (!(T.tank_ms_bits & TMS_TRIO) || (B_tms0 & TMS_TRIO)) MV_FAIL("A. B's count: A TRIO %s", T.tank_ms_bits & TMS_TRIO ? "was already there" : "not marked");
    live(2.0f);
    if (T.sd_balance != balB || T.sd_earned != earnedB) MV_FAIL("A. the move minted sand dollars: %d earned %d (was %d / %d)", (int)T.sd_balance, (int)T.sd_earned, (int)balB, (int)earnedB);
    if (collection_count() != coll0 || G.n_given || G.n_arr) MV_FAIL("A. a move left a visitor (%d) or a gift note (%d given, %d arriving)", collection_count(), G.n_given, G.n_arr);
    if (progression_newborn() >= 0 || progression_setup_pending()) MV_FAIL("A. the arrival owes a birth flow");
    boot();
    if (here(PIP) != 1 || bowl(PIP) || has_frame("move")) MV_FAIL("A. after a boot in B: %d in the tank, bowl %d", here(PIP), bowl(PIP));
    if (find(PIP) >= 0) { snap_t z; snap(find(PIP), &z); z.hunger = now.hunger; z.energy = now.energy; z.stress = now.stress; z.curiosity = now.curiosity; z.bored = now.bored; z.lazy = now.lazy;   /* (the save keeps the drives as it does every fish's) */
        z.age = now.age; z.lived = now.lived; z.size = now.size;
        const char *d = snap_diff(&now, &z); if (d) MV_FAIL("A. PIP after a boot in B: %s", d); }
    {   int a, b, w; if (copies(PIP, &a, &b, &w) != 1) MV_FAIL("A. PIP on the board: %d here, %d in the other tank, bowl %d", a, b, w); }
    /* B: back to A */
    { int i = find(PIP); snap(i, &was);
      if (progression_move_out(&T, i) != BOWL_OK) MV_FAIL("B. MOVE out of B refused (%d)", progression_move_why(&T, i)); }
    if (!swap_to(0)) { MV_FAIL("B. the swap to A"); return 1; }
    if (T.uid != A_uid || here(PIP)) MV_FAIL("B. A after the trip: its own PIP is %s", here(PIP) ? "still in its save (two of it)" : "gone");
    if (progression_bowl_let_in(&T) != BOWL_OK) MV_FAIL("B. LET IN at home refused (%d)", progression_bowl_why(&T));
    if (find(PIP) >= 0) {
        snap(find(PIP), &now);
        const char *d = snap_diff(&was, &now);
        if (d) MV_FAIL("B. PIP came home changed: %s", d);
        const fish_t *f = &T.fish[find(PIP)];
        if (f->parent_a != find(MA) || f->parent_b != find(PA)) MV_FAIL("B. PIP's parents' slots at home: %d / %d", f->parent_a, f->parent_b);
        if (now.from != A_uid) MV_FAIL("B. PIP's FROM at home is %08x", (unsigned)now.from);
    } else MV_FAIL("B. PIP is not home");
    {   int a, b, w; if (copies(PIP, &a, &b, &w) != 1 || a != 1) MV_FAIL("B. PIP on the board: %d here, %d in the other tank, bowl %d", a, b, w); }
    live(2.0f);
    if (T.sd_balance != balB || T.sd_earned != earned0) MV_FAIL("B. the round trip minted sand dollars: %d, earned at home %d (was %d, %d)", (int)T.sd_balance, (int)T.sd_earned, (int)balB, (int)earned0);
    printf("selftest-move: A-B a move there (two days in the bowl) and back: every field of the fish as it left, its parents named, no sand dollars, one fish\n");

    /* ---- C: the power cut at every write, out and in ---- */
    int runs = 0;
    for (int dir = 0; dir < 2; dir++) {               /* 0: MOVE out of A; 1: LET IN to B */
        if (!two_tanks()) { MV_FAIL("C. staging"); return 1; }
        if (dir == 1) { if (progression_move_out(&T, 2) != BOWL_OK || !swap_to(0)) { MV_FAIL("C. staging LET IN"); return 1; } }
        dir_copy(s_dir, s_pre);
        shelf_port_sim_cut(1000);
        int r = dir ? progression_bowl_let_in(&T) : progression_move_out(&T, find(PIP));
        int used = 1000 - shelf_port_sim_cut(-1);
        if (r != BOWL_OK || used < 2) MV_FAIL("C. dir %d uncut: %d, %d writes", dir, r, used);
        for (int n = 0; n <= used; n++)
            for (int m = 0; m <= 10; m++) {
                dir_copy(s_pre, s_dir); boot();
                shelf_port_sim_cut(n);
                if (dir) progression_bowl_let_in(&T); else progression_move_out(&T, find(PIP));
                shelf_port_sim_cut(-1);               /* the power gone at write n */
                shelf_port_sim_cut(m); boot(); int bl = shelf_port_sim_cut(-1);   /* the boot after it, cut at its write m */
                boot(); runs++;                       /* ... and a clean one */
                int in = here(PIP), w = bowl(PIP);
                if (in + w != 1) MV_FAIL("C. dir %d, cut %d/%d: %d in the tank, %d in the bowl (one fish, always)", dir, n, m, in, w);
                if (has_frame("move") != (w != 0)) MV_FAIL("C. dir %d, cut %d/%d: the record %s after a clean boot", dir, n, m, w ? "is gone with the fish in the bowl" : "was left");
                if (n == 0 && (dir ? in : w)) MV_FAIL("C. dir %d, cut 0: not the state before", dir);
                if (n >= 1 && (dir ? !in : !w)) MV_FAIL("C. dir %d, cut %d/%d: the record reached the flash, yet the boot %s", dir, n, m, dir ? "left the fish in the bowl" : "kept the fish in the tank");
                if (in && T.n_fish != 3) MV_FAIL("C. dir %d, cut %d/%d: %d fish", dir, n, m, T.n_fish);
                if (dir && in) { snap_t z; snap(find(PIP), &z); if (strcasecmp(z.name, "pip") || z.paid != was.paid || z.trust != 10.0f) MV_FAIL("C. dir 1, cut %d/%d: PIP made from the record wrong (%s, ledger %x)", n, m, z.name, (unsigned)z.paid); }
                if (bl > 0) break;                    /* (the boot finished before its cut) */
            }
        /* D: one write refused, the board running on */
        for (int k = 0; k < used; k++) {
            dir_copy(s_pre, s_dir); boot();
            shelf_port_sim_fail(k);
            r = dir ? progression_bowl_let_in(&T) : progression_move_out(&T, find(PIP));
            shelf_port_sim_fail(-1);
            if (k == 0) {                             /* the record's own write: nothing happened, and the card says so */
                if (r != BOWL_NO_WRITE || (dir ? here(PIP) || !bowl(PIP) : here(PIP) != 1 || bowl(PIP))) MV_FAIL("D. dir %d, the first write refused: %d, %d in the tank, bowl %d", dir, r, here(PIP), bowl(PIP));
            } else if (r != BOWL_OK) MV_FAIL("D. dir %d, write %d refused: answered %d", dir, k, r);
            if (!dir && k > 0 && (progression_fish_sellable(&T, 0) != progression_keeps_two(&T, 0))) MV_FAIL("D. write %d refused: the others' SELL changed", k);
            if (dir && k > 0 && here(PIP) == 1 && has_frame("move")) {   /* its record stands: not sold, given or moved */
                int i = find(PIP);
                if (progression_fish_sellable(&T, i) || progression_fish_givable(&T, i) || progression_move_why(&T, i) == BOWL_OK) MV_FAIL("D. write %d refused: PIP, its record standing, may be sold / given / moved", k);
            }
            live(0.5f); progression_save(&T);         /* the next save lands */
            if (dir && here(PIP) == 1 && has_frame("move")) MV_FAIL("D. dir 1, write %d refused: the record still stands after a save that landed", k);
            int a, b, w, c = copies(PIP, &a, &b, &w); runs++;
            if (c != 1) MV_FAIL("D. dir %d, write %d refused, a swap away and back: %d here, %d there, bowl %d", dir, k, a, b, w);
            boot();
            if (here(PIP) + bowl(PIP) != 1) MV_FAIL("D. dir %d, write %d refused, then a boot: %d in the tank, bowl %d", dir, k, here(PIP), bowl(PIP));
            /* ... and the power going right after the refused write instead */
            dir_copy(s_pre, s_dir); boot();
            shelf_port_sim_fail(k);
            if (dir) progression_bowl_let_in(&T); else progression_move_out(&T, find(PIP));
            shelf_port_sim_fail(-1);
            boot(); runs++;
            if (here(PIP) + bowl(PIP) != 1) MV_FAIL("D. dir %d, write %d refused, then the power cut: %d in the tank, bowl %d", dir, k, here(PIP), bowl(PIP));
        }
    }
    printf("selftest-move: C-D the power cut at every write of MOVE and LET IN and of the boot after, one write refused at every step: one fish, %d runs\n", runs);

    /* ---- E: the refusals and their lines ---- */
#define LINE_IS(want, what) do { const char *l_ = render_give_modal_line(&T); if (strcmp(l_, want)) MV_FAIL("E. %s: the card says \"%s\", want \"%s\"", what, l_, want); } while (0)
    if (!two_tanks()) { MV_FAIL("E. staging"); return 1; }
    /* a visitor, a fish next door, at a door, born next door, being given */
    if (progression_move_why(&T, N_FISH_MAX) != BOWL_NO_FISH || progression_move_out(&T, N_FISH_MAX) != BOWL_NO_FISH) MV_FAIL("E. a visitor may be moved");
    T.fish[2].away = true;
    if (progression_move_why(&T, 2) != BOWL_NO_AWAY) MV_FAIL("E. a fish next door may be moved");
    render_move_tap(&T, 2); if (render_give_modal_step(&T) != RENDER_GM_MOVE_WHY) MV_FAIL("E. next door: no card"); LINE_IS("VISITING NEXT DOOR", "next door");
    T.fish[2].away = false; T.fish[2].door = DOOR_PASSING;
    if (progression_move_why(&T, 2) != BOWL_NO_AWAY) MV_FAIL("E. a fish in a doorway may be moved");
    T.fish[2].door = DOOR_NONE; T.fish[2].born_away = true;
    if (progression_move_why(&T, 2) != BOWL_NO_AWAY) MV_FAIL("E. a fry born next door, not yet home, may be moved");
    T.fish[2].born_away = false;
    G.asking_uid = PIP;
    if (progression_move_why(&T, 2) != BOWL_NO_GIVING) MV_FAIL("E. a fish being given may be moved");
    render_move_tap(&T, 2); LINE_IS("pip IS BEING GIVEN", "mid-GIVE");
    G.asking_uid = 0; G.leaving_uid = PIP;
    if (progression_move_why(&T, 2) != BOWL_NO_GIVING) MV_FAIL("E. a fish GIVEN and leaving may be moved");
    G.leaving_uid = 0;
    if (progression_move_out(&T, 2) != BOWL_OK) MV_FAIL("E. PIP would not go");
    /* the last two */
    if (progression_move_why(&T, 0) != BOWL_NO_LAST_TWO || progression_move_out(&T, 0) != BOWL_NO_LAST_TWO || T.n_fish != 2) MV_FAIL("E. one of the last two may be moved");
    render_move_tap(&T, 0); if (render_give_modal_step(&T) != RENDER_GM_MOVE_WHY) MV_FAIL("E. the last two: no card"); LINE_IS("THE LAST TWO FISH STAY", "the last two");
    { int x, y; if (render_give_modal_button(&T, RENDER_GM_TAP_MOVE, &x, &y)) MV_FAIL("E. the last two: the card has a MOVE button");
      if (card_tap(RENDER_GM_TAP_OK) != RENDER_GM_TAP_OK || render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("E. the last two: OK did not close the card"); }
    /* a fry not yet named: itself, lit and dark; and the bowl taken */
    progression_force_arrival(&T);
    { int nb = progression_newborn();
      if (nb < 0 || !progression_welcome_owed(&T, nb)) MV_FAIL("E. staging a fry owed its welcome");
      else {
          if (progression_move_why(&T, nb) != BOWL_NO_FRY_NAME) MV_FAIL("E. a fry not yet named may be moved (%d)", progression_move_why(&T, nb));
          render_move_tap(&T, nb); LINE_IS("NAME THIS FRY FIRST", "a fry not yet named");
          T.light_auto = false; T.light_manual_off = true; T.night = true;
          render_move_tap(&T, nb); LINE_IS("LIGHT ON TO NAME THIS FRY", "a fry not yet named, in the dark");
          T.light_manual_off = false; T.night = false;
          while (progression_newborn() >= 0) progression_newborn_done(&T);
          if (progression_move_why(&T, nb) != BOWL_NO_BUSY || progression_move_out(&T, nb) != BOWL_NO_BUSY) MV_FAIL("E. a second fish may go into the bowl (%d)", progression_move_why(&T, nb));
          render_move_tap(&T, nb); LINE_IS("THE BOWL HAS pip", "the bowl taken");
          render_give_modal_close();
          if (here(PIP) || !bowl(PIP) || T.n_fish != 3) MV_FAIL("E. the refusals moved something");
      } }
    /* LET IN: a full tank - five fish, then four and a place held */
    while (T.n_fish < POP_CAP) tank_add_fish(&T, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    if (progression_bowl_why(&T) != BOWL_NO_FULL || progression_bowl_let_in(&T) != BOWL_NO_FULL || T.n_fish != POP_CAP || !bowl(PIP)) MV_FAIL("E. a full tank took the fish (%d)", progression_bowl_why(&T));
    render_bowl_card(&T); if (render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("E. full: no IS WAITING card"); LINE_IS("THIS TANK IS FULL", "a full tank");
    { int r = card_tap(RENDER_GM_TAP_LETIN);
      if (r != RENDER_GM_TAP_CARD || T.n_fish != POP_CAP || !bowl(PIP) || render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("E. full: a tap on the dim LET IN did something (%d)", r);
      if (card_tap(RENDER_GM_TAP_BACK) != RENDER_GM_TAP_BACK || render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("E. full: NOT NOW did not close the card"); }
    { int i = T.n_fish - 1; if (!progression_sell_fish(&T, i)) MV_FAIL("E. staging four fish");
      T.fry_hold[0].peer = 0x1234u; T.fry_hold[0].until = T.clock + 600;
      if (progression_bowl_why(&T) != BOWL_NO_FULL) MV_FAIL("E. four fish and a place held: the bowl's fish would take the held place");
      T.fry_hold[0].peer = 0;
      if (progression_bowl_why(&T) != BOWL_OK) MV_FAIL("E. four fish: no room (%d)", progression_bowl_why(&T)); }
    /* the wrong habitat: another kind of creature; a tank type this build does not know; the jelly tank */
    progression_debug_bowl_kind(1); boot();
    if (progression_bowl_why(&T) != BOWL_NO_KIND || progression_bowl_let_in(&T) != BOWL_NO_KIND || !bowl(PIP)) MV_FAIL("E. a fish tank took another kind of creature");
    render_bowl_card(&T); LINE_IS("pip CAN'T LIVE HERE", "another kind of creature"); render_give_modal_close();
    progression_debug_bowl_kind(PASSPORT_KIND_FISH); boot();
    progression_debug_bowl_type(7);
    if (progression_bowl_why(&T) != BOWL_NO_KIND || progression_bowl_let_in(&T) != BOWL_NO_KIND || !bowl(PIP)) MV_FAIL("E. a tank type this build does not know took the fish");
    progression_debug_bowl_type(-1);
    {   int j = shelf_new_kind(SHELF_TYPE_JELLY, 0, clock_port_now_unix());
        if (j < 0) MV_FAIL("E. no jelly tank");
        else {
            if (!swap_to(j)) { MV_FAIL("E. the swap to the jelly tank"); return 1; }
            if (shelf_now()->type != SHELF_TYPE_JELLY || !progression_is_jelly()) MV_FAIL("E. the jelly tank is not on the glass");
            if (!bowl(PIP)) MV_FAIL("E. the bowl lost its fish on the way to the jelly tank");
            if (progression_bowl_why(&T) != BOWL_NO_KIND || progression_bowl_let_in(&T) != BOWL_NO_KIND || !bowl(PIP) || here(PIP)) MV_FAIL("E. the jelly tank took a fish");
            int back = -1; for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->type == SHELF_TYPE_FISH && shelf_get(i)->pos == 0) back = i;
            if (back < 0 || !swap_to(back)) { MV_FAIL("E. the swap back from the jelly tank"); return 1; }
            if (progression_bowl_let_in(&T) != BOWL_OK || here(PIP) != 1) MV_FAIL("E. home from the jelly tank: LET IN refused (%d)", progression_bowl_why(&T));
        }
    }
    printf("selftest-move: E every refusal, each with its line: the last two, a fry not named, the bowl taken, next door, being given; a full tank, the wrong habitat, the jelly tank\n");

    /* ---- F: the bowl is the board's ---- */
    if (!two_tanks() || progression_move_out(&T, 2) != BOWL_OK) { MV_FAIL("F. staging"); return 1; }
    if (shelf_new(SHELF_TYPE_FISH, clock_port_now_unix()) < 0 || !bowl(PIP)) MV_FAIL("F. a NEW TANK emptied the bowl");
    boot(); if (!bowl(PIP)) MV_FAIL("F. a NEW TANK, then a boot: the bowl is empty");
    if (!shelf_remove(shelf_count() - 1) || !bowl(PIP)) MV_FAIL("F. a REMOVE emptied the bowl");
    if (!shelf_remove(0) || shelf_count() != 0 || !bowl(PIP)) MV_FAIL("F. the REMOVE of the last other tank emptied the bowl");
    boot(); if (!bowl(PIP) || here(PIP)) MV_FAIL("F. after the REMOVEs and a boot: bowl %d", bowl(PIP));
    progression_reset(&T, 99);
    if (!bowl(PIP) || !has_frame("move")) MV_FAIL("F. RESET THIS TANK emptied the bowl");
    boot(); progression_setup_done(&T);
    if (!bowl(PIP) || here(PIP)) MV_FAIL("F. RESET THIS TANK, then a boot: bowl %d", bowl(PIP));
    if (progression_bowl_let_in(&T) != BOWL_OK || here(PIP) != 1 || T.n_fish != 3) MV_FAIL("F. LET IN to the reset tank refused (%d)", progression_bowl_why(&T));
    { snap_t z; snap(find(PIP), &z); if (z.paid != was.paid || z.trust != 10.0f || strcasecmp(z.name, "pip")) MV_FAIL("F. PIP in the reset tank: ledger %x", (unsigned)z.paid); }
    /* ERASE EVERYTHING */
    if (!two_tanks() || progression_move_out(&T, 2) != BOWL_OK) { MV_FAIL("F. staging the erase"); return 1; }
    if (!progression_erase_all(&T, 4242, false, NULL)) MV_FAIL("F. ERASE EVERYTHING refused");
    if (progression_bowl_has() || progression_bowl_uid() || has_frame("move")) MV_FAIL("F. ERASE EVERYTHING left the bowl's fish (record %d)", has_frame("move"));
    if (progression_bowl_let_in(&T) == BOWL_OK || T.n_fish != 2) MV_FAIL("F. a fish came out of an erased bowl");
    boot();
    if (progression_bowl_has() || has_frame("move") || here(PIP) || T.n_fish != 2) MV_FAIL("F. ERASE EVERYTHING, then a boot: the bowl's fish is back");
    printf("selftest-move: F the bowl through a NEW TANK, two REMOVEs and RESET THIS TANK: its fish kept; ERASE EVERYTHING: gone\n");

    /* ---- G: nothing to farm ---- */
    if (!two_tanks()) { MV_FAIL("G. staging"); return 1; }
    {   const int32_t b0 = T.sd_balance, e0 = T.sd_earned; int32_t eB = 0; uint32_t tmsA = 0, tmsB = 0; snap_t first; snap(2, &first);
        if (!swap_to(0)) return 1;
        eB = T.sd_earned;                             /* (what a tank earned is its own: B's) */
        if (T.sd_balance != b0 || !swap_to(0)) { MV_FAIL("G. staging: B's wallet"); return 1; }
        for (int trip = 0; trip < 20; trip++) {
            if (progression_move_out(&T, find(PIP)) != BOWL_OK || !swap_to(0)) { MV_FAIL("G. trip %d: out of A", trip); break; }
            if (progression_bowl_let_in(&T) != BOWL_OK) { MV_FAIL("G. trip %d: into B", trip); break; }
            live(1.5f);                               /* (anything B would pay for the arrival, it pays now) */
            if (trip == 0) tmsB = T.tank_ms_bits; else if (T.tank_ms_bits != tmsB) MV_FAIL("G. trip %d: B's badges moved (%x -> %x)", trip, (unsigned)tmsB, (unsigned)T.tank_ms_bits);
            if (T.sd_balance != b0 || T.sd_earned != eB) { MV_FAIL("G. trip %d in B: the wallet %d, earned %d (were %d, %d)", trip, (int)T.sd_balance, (int)T.sd_earned, (int)b0, (int)eB); break; }
            if (progression_move_out(&T, find(PIP)) != BOWL_OK || !swap_to(0)) { MV_FAIL("G. trip %d: out of B", trip); break; }
            if (progression_bowl_let_in(&T) != BOWL_OK) { MV_FAIL("G. trip %d: home", trip); break; }
            live(1.5f);
            if (trip == 0) tmsA = T.tank_ms_bits; else if (T.tank_ms_bits != tmsA) MV_FAIL("G. trip %d: A's badges moved", trip);
            if (T.sd_balance != b0 || T.sd_earned != e0) { MV_FAIL("G. trip %d at home: the wallet %d, earned %d (were %d, %d)", trip, (int)T.sd_balance, (int)T.sd_earned, (int)b0, (int)e0); break; }
        }
        if (find(PIP) >= 0) { snap_t z; snap(find(PIP), &z); if (z.paid != first.paid || z.ms_bits != first.ms_bits || z.stage != first.stage || z.eaten != first.eaten) MV_FAIL("G. twenty trips changed PIP's ledger or badges"); }
        int a, b, w; if (copies(PIP, &a, &b, &w) != 1) MV_FAIL("G. after twenty trips: %d here, %d there, bowl %d", a, b, w);
        if (notice_pending() > 8) MV_FAIL("G. the trips queued %d notices", notice_pending());
    }
    printf("selftest-move: G twenty trips there and back: the wallet, what was earned and both tanks' badges the same\n");

    /* ---- H: the IS WAITING card's manners ---- */
    if (!two_tanks() || progression_move_out(&T, 2) != BOWL_OK) { MV_FAIL("H. staging"); return 1; }
    for (int i = 0; i < 50; i++) notice_tick(&T, 0.1f, false);
    if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("H. the card came up right after MOVE, in the tank the fish just left");
    boot();
    T.light_auto = false; T.light_manual_off = true; T.night = true;      /* a dark tank */
    for (int i = 0; i < 50; i++) notice_tick(&T, 0.1f, false);
    if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("H. the card came up in the dark");
    T.light_manual_off = false; T.night = false;
    for (int i = 0; i < 50; i++) notice_tick(&T, 0.1f, true);              /* a page over the tank */
    if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("H. the card came up over a page");
    notice_updated();                                                     /* a card already queued: it goes first */
    notice_tick(&T, 0.1f, false);
    if (!notice_current() || render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("H. the card did not wait for the queued notice");
    live(1.0f); notice_dismiss();
    for (int i = 0; i < 200 && render_give_modal_step(&T) == RENDER_GM_OFF; i++) notice_tick(&T, 0.1f, notice_current() != NULL && false);
    if (render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("H. the light on, nothing over the tank: the card did not come (step %d)", render_give_modal_step(&T));
    else {
        if (strcmp(render_give_modal_line(&T), "LET IT INTO THIS TANK?")) MV_FAIL("H. the card asks \"%s\"", render_give_modal_line(&T));
        if (card_tap(RENDER_GM_TAP_BACK) != RENDER_GM_TAP_BACK || render_give_modal_step(&T) != RENDER_GM_OFF || !bowl(PIP)) MV_FAIL("H. NOT NOW did not close the card and keep the fish in the bowl");
    }
    for (int i = 0; i < 300; i++) notice_tick(&T, 0.1f, false);
    if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("H. after NOT NOW the card came back by itself");
    {   int x, y, w, h;                               /* the waiting row brings it back */
        render_frame_begin(); render_milestones(&T, fa, TANK_W);
        if (!render_bowl_row(&x, &y, &w, &h)) MV_FAIL("H. no waiting row on the keeper page");
        else {
            if (!on_glass(x + 40, y + 2) || !on_glass(x + w - 60, y + h - 2)) MV_FAIL("H. the waiting row is off the glass (%d,%d %dx%d)", x, y, w, h);
            int lit = 0; for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) lit += fa[yy * TANK_W + xx] == 0xffff;
            if (lit < 40) MV_FAIL("H. the waiting row's words are not drawn (%d white pixels)", lit);
            if (render_milestones_tap(&T, (float)(x + w / 2), (float)(y + h / 2)) != MS_TAP_KEPT || render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("H. a tap on the waiting row did not bring the card");
            render_frame_begin(); render_milestones(&T, fa, TANK_W);
            if (card_tap(RENDER_GM_TAP_LETIN) != RENDER_GM_TAP_LETIN) MV_FAIL("H. LET IN on the card was not taken");
            if (here(PIP) != 1 || bowl(PIP) || render_give_modal_step(&T) != RENDER_GM_DONE) MV_FAIL("H. LET IN on the card: %d in the tank, bowl %d, card %d", here(PIP), bowl(PIP), render_give_modal_step(&T));
            /* its double tap: the card is gone, and the second tap reaches nothing under it (the keeper page's rows) */
            { int fx = PAGE_X + 100, fy = PAGE_Y + MSP_ROW_Y0 + MSP_ROW_MID;   /* (the first fish's row: a tap there opens its card) */
              if (render_milestones_tap(&T, (float)fx, (float)fy) != MS_TAP_KEPT || render_milestones_card(&T, NULL, NULL, NULL, NULL)) MV_FAIL("J7. LET IN's double tap opened what was under the card");
              if (render_give_modal_tap(&T, (float)fx, (float)fy) != RENDER_GM_TAP_HELD) MV_FAIL("J7. a tap right after LET IN was not spent");
              live(TAP_W_S + 0.1f);
              if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("J7. the card's spent moment never ends (step %d)", render_give_modal_step(&T)); }
            if (render_bowl_row(&x, &y, &w, &h)) MV_FAIL("H. the waiting row stays with the bowl empty");
        }
        render_milestones_leave();
    }
    boot();
    for (int i = 0; i < 100; i++) notice_tick(&T, 0.1f, false);
    if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("H. a card with the bowl empty");
    if (progression_move_out(&T, find(PIP)) != BOWL_OK) MV_FAIL("H. staging the next start");
    boot();
    for (int i = 0; i < 100 && render_give_modal_step(&T) == RENDER_GM_OFF; i++) notice_tick(&T, 0.1f, false);
    if (render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("H. the next start: the card did not come again");
    render_give_modal_close();
    printf("selftest-move: H the IS WAITING card: once a start, with the light, after the notices, never over a page; NOT NOW holds; the waiting row brings it back\n");

    /* ---- I: this glass ---- */
    if (!two_tanks()) { MV_FAIL("I. staging"); return 1; }
    snprintf(T.fish[2].name, sizeof T.fish[2].name, "wwwwwww");            /* the longest name */
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;   /* linked: GIVE on the card too - five buttons */
    render_frame_begin(); render_milestones(&T, fa, TANK_W);
    render_milestones_show_fish(&T, 2);
    render_frame_begin(); render_milestones(&T, fa, TANK_W);
    {   int fish, rx, sx, by, gx, gy, mx, my, ox, oy;
        if (!render_milestones_card(&T, &fish, &rx, &sx, &by) || !render_milestones_give(&T, &gx, &gy) || !render_milestones_move(&T, &mx, &my) || !render_milestones_more(&T, &ox, &oy))
            MV_FAIL("I. the card does not show all five buttons");
        else {
            const int X[5] = { rx, gx, mx, sx, ox };
            const int cx0 = PAGE_X + MSP_MODAL_X, cx1 = cx0 + MSP_MODAL_W;
            for (int k = 0; k < 5; k++) {
                if (k && X[k] - X[k - 1] < MSP_CARD_ICON + 8) MV_FAIL("I. the card's buttons %d and %d are %d px apart", k - 1, k, X[k] - X[k - 1]);
                if (X[k] - MSP_CARD_ICON / 2 < cx0 + 4 || X[k] + MSP_CARD_ICON / 2 > cx1 - 4) MV_FAIL("I. the card's button %d (x %d) is off the card (%d..%d)", k, X[k], cx0, cx1);
                if (!on_glass(X[k] - MSP_CARD_ICON / 2, by + MSP_CARD_ICON / 2) || !on_glass(X[k] + MSP_CARD_ICON / 2, by + MSP_CARD_ICON / 2)) MV_FAIL("I. the card's button %d is off the glass", k);
            }
            /* a tap on MOVE: its card, and only its card - nothing moved by one tap */
            if (render_milestones_tap(&T, (float)mx, (float)my) != MS_TAP_KEPT || render_give_modal_step(&T) != RENDER_GM_MOVE || T.n_fish != 3 || progression_bowl_has()) MV_FAIL("I. a tap on MOVE: step %d, %d fish", render_give_modal_step(&T), T.n_fish);
            live(0.1f);
            if (T.n_fish != 3 || progression_bowl_has()) MV_FAIL("I. one tap on MOVE moved the fish");
            /* the card's words fit it */
            char l[48]; snprintf(l, sizeof l, "PUT %s", T.fish[2].name);
            const int right = 336 - 12 - 100 - 12 - 12;
            if (render_text_w(l, 2) > right || render_text_w("TRAVEL BOWL?", 2) > right || render_text_w("THE TRAVEL BOWL", 2) > right) MV_FAIL("I. the card's words beside the fish are wider than their %d px", right);
            static const char *const LINES[] = { "IT WAITS THERE UNTIL YOU", "LET IT INTO A TANK", "THE LAST TWO FISH STAY", "LIGHT ON TO NAME THIS FRY", "THE BOWL HAS wwwwwww", "VISITING NEXT DOOR",
                "wwwwwww IS BEING GIVEN", "THIS TANK IS FULL", "wwwwwww CAN'T LIVE HERE", "LET IT INTO THIS TANK?", "wwwwwww IS WAITING", "TRAVEL BOWL" };
            for (unsigned k = 0; k < sizeof LINES / sizeof LINES[0]; k++) if (render_text_w(LINES[k], 2) > 336 - 24) MV_FAIL("I. \"%s\" is wider than the card", LINES[k]);
            if (render_text_w("THAT DID NOT WORK.", 2) > 336 - 24 || strlen("THAT DID NOT WORK. TRY AGAIN") > 31) MV_FAIL("I. the refused-write line does not fit");
            if (render_text_w("TAP HERE TO LET IT IN", 2) > PAGE_W - 92 - 40 || render_text_w("wwwwwww WAITS IN THE BOWL", 2) > PAGE_W - 92 - 40) MV_FAIL("I. the waiting row's words are wider than the row");
            if (render_text_w("wwwwwww WAITS", 2) > render_text_w("MY TANKS", 3) + 2 * 12) MV_FAIL("I. MY TANKS's line is wider than its title's box and the padlock's gap");
            /* the MOVE card's buttons: on the glass; BACK closes; MOVE moves */
            int bx, bby, kx, ky;
            if (!render_give_modal_button(&T, RENDER_GM_TAP_MOVE, &bx, &bby) || !render_give_modal_button(&T, RENDER_GM_TAP_BACK, &kx, &ky)) MV_FAIL("I. the MOVE card has no MOVE / BACK");
            else {
                if (!on_glass(bx - 70, bby + 17) || !on_glass(kx + 70, ky + 17) || kx - bx < 150) MV_FAIL("I. the MOVE card's buttons: MOVE at %d,%d BACK at %d,%d", bx, bby, kx, ky);
                if (render_give_modal_tap(&T, (float)bx, (float)bby) != RENDER_GM_TAP_HELD) MV_FAIL("I. MOVE took the double of the tap that opened its card");
                if (card_tap(RENDER_GM_TAP_BACK) != RENDER_GM_TAP_BACK || render_give_modal_step(&T) != RENDER_GM_OFF || T.n_fish != 3) MV_FAIL("I. BACK did not close the MOVE card");
                render_milestones_tap(&T, (float)mx, (float)my);
                render_frame_begin(); render_milestones(&T, fa, TANK_W);
                const uint32_t W7 = T.fish[2].uid;
                if (card_tap(RENDER_GM_TAP_MOVE) != RENDER_GM_TAP_MOVE) MV_FAIL("I. MOVE on its card was not taken");
                if (here(W7) || !bowl(W7) || render_give_modal_step(&T) != RENDER_GM_DONE) MV_FAIL("I. MOVE on its card: %d in the tank, bowl %d, card %d", here(W7), bowl(W7), render_give_modal_step(&T));
                if (render_milestones_tap(&T, (float)(PAGE_X + 100), (float)(PAGE_Y + MSP_ROW_Y0 + MSP_ROW_MID)) != MS_TAP_KEPT || render_milestones_card(&T, NULL, NULL, NULL, NULL)) MV_FAIL("J7. MOVE's double tap reached the page under the card");
                live(TAP_W_S + 0.1f);
                if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("J7. after MOVE the card's spent moment never ends");
                render_frame_begin(); render_milestones(&T, fa, TANK_W);
                if (render_milestones_card(&T, &fish, &rx, &sx, &by)) MV_FAIL("I. the card of the fish that left is still up");
            }
        }
        render_milestones_leave();
    }
    /* a board with one tank and an empty bowl: no MOVE on the card (nowhere to move to) */
    board_new(); boot(); progression_setup_done(&T);
    while (T.n_fish < 3) tank_add_fish(&T, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    render_frame_begin(); render_milestones(&T, fa, TANK_W); render_milestones_show_fish(&T, 2);
    render_frame_begin(); render_milestones(&T, fa, TANK_W);
    { int x, y; if (render_milestones_move(&T, &x, &y)) MV_FAIL("I. one tank, an empty bowl: the card shows MOVE"); }
    render_milestones_leave();
    printf("selftest-move: I this glass (%d x %d): the card's five buttons apart, on the card and the glass; one tap moves nothing; the cards' buttons and words fit\n", TANK_W, TANK_H);

    /* ---- J: the review of 82df24c (each case red before its fix) ---- */
    /* J1 gift notes: the tank the fish would enter gave it away once (its GIVEN note stands) - LET IN there would be undone
       by that tank's boot, the record already gone: none of the fish */
    if (!two_tanks()) { MV_FAIL("J. staging"); return 1; }
    {   passport_t pp; passport_from_fish(&pp, &T.fish[2], T.uid, 100);
        if (!swap_to(0)) return 1;
        if (!gift_given_add(&G, PIP, 0x1111u, 0xb0b0b0b0u, &pp) || !progression_save(&T)) MV_FAIL("J1. staging B's GIVEN note");
        if (!swap_to(0)) return 1;
        if (G.n_given) MV_FAIL("J1. staging: A has B's note");
        if (progression_move_out(&T, find(PIP)) != BOWL_OK || !swap_to(0)) { MV_FAIL("J1. staging the move"); return 1; }
        if (G.n_given != 1) MV_FAIL("J1. staging: B's note did not come back with B (%d)", G.n_given);
        int w = progression_bowl_why(&T), r = progression_bowl_let_in(&T);
        if (w != BOWL_NO_NOTE || r != BOWL_NO_NOTE) MV_FAIL("J1. a tank holding a GIVEN note for the fish let it in (%d, %d)", w, r);
        render_bowl_card(&T); LINE_IS("pip CAN'T COME IN YET", "a tank that gave the fish away"); render_give_modal_close();
        boot();
        int a, b, c; if (copies(PIP, &a, &b, &c) != 1) MV_FAIL("J1. B's GIVEN note and the bowl: %d here, %d there, bowl %d (one fish)", a, b, c);
        /* ... and the tank it would leave holds a note for it (back from a lost GIVE, its save not landed): MOVE would leave
           the BACK note to remake it at this tank's next boot - two of the fish */
        if (!swap_to(0)) return 1;                    /* A on the glass, the fish in the bowl */
        if (progression_bowl_let_in(&T) != BOWL_OK) MV_FAIL("J1. home: LET IN refused (%d)", progression_bowl_why(&T));
        int i = find(PIP); passport_from_fish(&pp, &T.fish[i], T.uid, 100);
        shelf_port_sim_cut(0);                        /* (the notes in RAM only: as after a save the flash refused) */
        G.given[0] = (gift_note_t){ 0 }; G.given[0].uid = PIP; G.given[0].nonce = 0x2222u; G.given[0].pp = pp; G.given[0].back = true; G.n_given = 1;
        shelf_port_sim_cut(-1);
        if (progression_move_why(&T, i) != BOWL_NO_GIVING || progression_move_out(&T, i) != BOWL_NO_GIVING || !here(PIP)) MV_FAIL("J1. a fish with a BACK note not settled may be moved (%d)", progression_move_why(&T, i));
        render_move_tap(&T, i); LINE_IS("pip IS BEING GIVEN", "a gift note not settled"); render_give_modal_close();
        G.n_given = 0; G.arriving[0] = (gift_note_t){ 0 }; G.arriving[0].uid = PIP; G.n_arr = 1;
        if (progression_move_why(&T, i) != BOWL_NO_GIVING) MV_FAIL("J1. a fish with an ARRIVING note may be moved");
        G.n_arr = 0;
        /* ... and the other way: a gift of the very fish the bowl holds is not taken in */
        if (progression_move_out(&T, i) != BOWL_OK) MV_FAIL("J1. staging the bowl");
        if (progression_gift_adopt(&T, &pp) >= 0 || here(PIP)) MV_FAIL("J1. a gift of the fish the bowl holds was taken in (two of it)");
    }
    /* J2 the card's room: never under the rename wheel, the reset prompt or a tool in hand - whatever `blocked` says - and
       its one offer is still owed after */
    boot();
#define NO_CARD(what) do { for (int i_ = 0; i_ < 60; i_++) notice_tick(&T, 0.1f, false); \
        if (render_give_modal_step(&T) != RENDER_GM_OFF) { MV_FAIL("J2. the card came up %s", what); render_give_modal_close(); } } while (0)
    setup_begin_rename(&T, 0); NO_CARD("under the rename wheel"); setup_cancel(&T);
    render_reset_open(RESET_PG_CHOOSE); NO_CARD("under the reset prompt"); render_reset_close();
    tank_set_tool(&T, TOOL_BREED); NO_CARD("with the BREEDING tool in hand"); tank_set_tool(&T, TOOL_HAND);
    if (notice_card_room(&T) != true) MV_FAIL("J2. no room for the card on a bare tank");
    for (int i = 0; i < 60 && render_give_modal_step(&T) == RENDER_GM_OFF; i++) notice_tick(&T, 0.1f, false);
    if (render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("J2. the offer was spent under a page: the card never came");
    /* J3 a press from before the card came is no NOT NOW */
    {   int x = 0, y = 0; live(0.5f);
        render_frame_begin(); render_tank(&T, fa, TANK_W); render_give_modal(&T, fa, TANK_W);
        render_give_modal_button(&T, RENDER_GM_TAP_BACK, &x, &y);
        render_bowl_card(&T);                         /* (it comes now: the finger went down a moment ago) */
        render_tap_press(T.clock - 0.05);
        int r = render_give_modal_tap(&T, (float)x, (float)y);
        if (r != RENDER_GM_TAP_HELD || render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("J3. a press from before the card answered NOT NOW (%d)", r);
        render_tap_press_end();
    }
    /* LOW BATTERY through a waiting card (the boards block the notices while a card is up); the card owed again after */
    notice_low_battery();
    for (int i = 0; i < 40 && !notice_current(); i++) notice_tick(&T, 0.1f, render_give_modal_step(&T) != RENDER_GM_OFF);
    if (!notice_current() || notice_current()->kind != NOTICE_LOW_BATTERY || render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("J2. LOW BATTERY did not get through the waiting card");
    live(0.5f); notice_dismiss();
    for (int i = 0; i < 200 && render_give_modal_step(&T) == RENDER_GM_OFF; i++) notice_tick(&T, 0.1f, render_give_modal_step(&T) != RENDER_GM_OFF);
    if (render_give_modal_step(&T) != RENDER_GM_BOWL) MV_FAIL("J2. after LOW BATTERY the card did not come back");
    /* ... and unanswered it steps aside by itself, for the rest of the start */
    live(RENDER_BOWL_CARD_S + 1.0f);
    if (render_give_modal_step(&T) != RENDER_GM_OFF || !bowl(PIP)) MV_FAIL("J2. an unanswered card never steps aside");
    for (int i = 0; i < 100; i++) notice_tick(&T, 0.1f, false);
    if (render_give_modal_step(&T) != RENDER_GM_OFF) MV_FAIL("J2. the card that stepped aside came back by itself");
    printf("selftest-move: J1-J3 gift notes in the tank left and the tank entered; the card's room, LOW BATTERY through it, its own time; a press from before it\n");

    /* J4 ERASE EVERYTHING still owed (a write refused): no MOVE until it is done - its retry deletes the bowl's record */
    {   bool owed = false;
        for (int k = 1; k < 12 && !owed; k++) {
            if (!two_tanks()) { MV_FAIL("J4. staging"); return 1; }
            shelf_port_sim_fail(k); bool ok = progression_erase_all(&T, 777, false, NULL); shelf_port_sim_fail(-1);
            owed = ok && has_frame("wipe");
        }
        if (!owed) MV_FAIL("J4. staging: no refused write leaves the erase owed");
        else {
            while (T.n_fish < 3) tank_add_fish(&T, 0, 1);
            while (progression_newborn() >= 0) progression_newborn_done(&T);
            const uint32_t X = T.fish[2].uid;
            if (progression_move_why(&T, 2) != BOWL_NO_WRITE || progression_move_out(&T, 2) != BOWL_NO_WRITE || !here(X)) MV_FAIL("J4. MOVE with the erase still owed (%d)", progression_move_why(&T, 2));
            for (int q = 0; q < 4 && has_frame("wipe"); q++) progression_save(&T);
            if (has_frame("wipe")) MV_FAIL("J4. the owed erase never finished");
            if (here(X) != 1) MV_FAIL("J4. the fish is gone after the erase's retry");
            boot(); if (here(X) + bowl(X) != 1) MV_FAIL("J4. after the retry and a boot: %d in the tank, bowl %d", here(X), bowl(X));
        }
    }
    /* J5 a shelf ID used again: an IN record whose tank was REMOVEd must not be finished in a NEW TANK wearing its ID */
    for (int k = -1; k < 14; k++) {                   /* -1: clean; else that boot's write k refused (the rewrite to OUT among them) */
        if (!two_tanks() || progression_move_out(&T, 2) != BOWL_OK || !swap_to(0)) { MV_FAIL("J5. staging"); return 1; }
        const uint32_t B_id = shelf_now()->id;
        dir_copy(s_dir, s_pre);
        shelf_port_sim_cut(1000); progression_bowl_let_in(&T); int used = 1000 - shelf_port_sim_cut(-1);
        for (int q = used - 1; q >= 1; q--) {         /* (as before LET IN: now with the record's delete - one of its last writes - refused) */
            dir_copy(s_pre, s_dir); boot();
            shelf_port_sim_fail(q); progression_bowl_let_in(&T); shelf_port_sim_fail(-1);
            if (has_frame("move") && here(PIP) == 1) break;
        }
        if (!has_frame("move") || here(PIP) != 1) { MV_FAIL("J5. staging: LET IN with its record's delete refused (record %d, here %d)", has_frame("move"), here(PIP)); break; }
        /* SWAP IN A, the record's delete refused again at the swap's save */
        shelf_port_sim_fail(1); bool sw = progression_save(&T); shelf_port_sim_fail(-1);
        progression_carry_t c; progression_carry_take(&T, &c);
        if (!sw || !has_frame("move") || !shelf_swap(0, NULL, 0, 0, &c, sizeof c, clock_port_now_unix())) { MV_FAIL("J5. staging: the swap with the IN record standing"); break; }
        boot();
        if (here(PIP) || bowl(PIP) || !has_frame("move")) MV_FAIL("J5. in A with B's IN record: %d here, bowl %d, record %d (it waits for B)", here(PIP), bowl(PIP), has_frame("move"));
        if (progression_move_why(&T, 0) == BOWL_OK) MV_FAIL("J5. another fish may go into a bowl whose record stands");
        if (!shelf_remove(0)) { MV_FAIL("J5. REMOVE B"); break; }   /* B goes, the fish with it, the record still IN */
        shelf_port_frame_del("next");                 /* the shelf's ID counter lost, and a boot that could write nothing (the record
                                                         stays IN): the next NEW TANK wears the REMOVEd tank's ID */
        shelf_port_sim_cut(0); boot(); shelf_port_sim_cut(-1);
        if (!has_frame("move")) { MV_FAIL("J5. staging: the record went at a boot that could not write"); break; }
        int j = shelf_new(SHELF_TYPE_FISH, clock_port_now_unix());
        if (j < 0) { MV_FAIL("J5. NEW TANK"); break; }
        const bool reused = shelf_get(j)->id == B_id;
        if (!swap_to(j)) { MV_FAIL("J5. the swap to the new tank"); break; }
        if (k >= 0) { shelf_port_sim_fail(k); boot(); shelf_port_sim_fail(-1); if (here(PIP)) MV_FAIL("J5. write %d refused: the fish made in the NEW TANK", k); boot(); }
        if (here(PIP)) MV_FAIL("J5. the IN record of a REMOVEd tank was finished in the NEW TANK%s", reused ? " wearing its shelf ID" : "");
        if (!bowl(PIP)) MV_FAIL("J5.%d the fish does not wait in the bowl again (record %d)", k, has_frame("move"));
        if (!reused) MV_FAIL("J5. staging: the NEW TANK does not wear the REMOVEd tank's shelf ID (%u): the case is not tested", (unsigned)B_id);
        if (progression_bowl_let_in(&T) != BOWL_OK || here(PIP) != 1) MV_FAIL("J5.%d LET IN to the new tank refused (%d)", k, progression_bowl_why(&T));
    }
    /* J6 a stage crossed in the night of a cut: paid once, by the frozen ledger - never in the tank the fish left too */
    {   int32_t gain[2] = { 0, 0 };
        for (int cut = 0; cut < 2; cut++) {           /* 0: the move whole; 1: the power gone between the record and the save */
            if (!two_tanks()) { MV_FAIL("J6. staging"); return 1; }
            progression_set_age(&T, 2, STAGE_ADULT_AGE - 900.0f); T.sd_paid_fish[2] = SD_PAID_JUV | SD_PAID_TRUST;
            T.fish[2].ms_bits &= ~(uint32_t)MS_REACHED_ADULT;
            if (T.fish[2].stage != STAGE_JUV || !progression_save(&T)) { MV_FAIL("J6. staging a juvenile"); break; }
            const int32_t b0 = T.sd_balance;
            if (cut) shelf_port_sim_cut(1);
            progression_move_out(&T, 2);
            shelf_port_sim_cut(-1);
            if (!cut) progression_save(&T);
            sim_clock_shift(3 * 86400);
            boot(); live(3.0f);
            if (here(PIP) || !bowl(PIP)) MV_FAIL("J6. cut %d: after the night %d in the tank, bowl %d", cut, here(PIP), bowl(PIP));
            gain[cut] = T.sd_balance - b0;
            if (progression_bowl_let_in(&T) != BOWL_OK) { MV_FAIL("J6. LET IN"); break; }
            snap_t z; snap(find(PIP), &z);
            if (z.stage != STAGE_JUV || (z.paid & SD_PAID_ADULT)) MV_FAIL("J6. cut %d: the bowl's fish grew or was paid in its night (stage %d, ledger %x)", cut, z.stage, (unsigned)z.paid);
        }
        if (gain[1] != gain[0]) MV_FAIL("J6. the night of the cut paid %d, a whole move's night %d: a stage paid in the tank the fish had left", (int)gain[1], (int)gain[0]);
    }
    /* J9 THE SWAP between MOVE and LET IN, and the swap after LET IN: the power cut at every write of it and of the boot
       after, one write refused at each step, a save refused at swap time */
    {   int sruns = 0;
        for (int side = 0; side < 2; side++) {        /* 0: the fish in the bowl, A -> B; 1: the fish let into B, B -> A */
            if (!two_tanks() || progression_move_out(&T, 2) != BOWL_OK) { MV_FAIL("J9. staging"); return 1; }
            if (side) { if (!swap_to(0) || progression_bowl_let_in(&T) != BOWL_OK) { MV_FAIL("J9. staging the far side"); return 1; } }
            dir_copy(s_dir, s_pre);
            shelf_port_sim_cut(1000); bool ok = swap_to(0); int used = 1000 - shelf_port_sim_cut(-1);
            if (!ok) { MV_FAIL("J9. the swap, uncut"); break; }
            for (int n = 0; n <= used; n++)
                for (int mode = 0; mode < 2; mode++) {    /* 0: the power gone at write n; 1: write n alone refused */
                    dir_copy(s_pre, s_dir); boot();
                    if (mode) shelf_port_sim_fail(n); else shelf_port_sim_cut(n);
                    bool sv = progression_save(&T);
                    if (sv) { progression_carry_t c; progression_carry_take(&T, &c); shelf_swap(0, NULL, 0, 0, &c, sizeof c, clock_port_now_unix()); }
                    if (mode) { boot(); shelf_port_sim_fail(-1); } else { shelf_port_sim_cut(-1); boot(); }
                    boot(); sruns++;
                    int a, b, w, c2 = copies(PIP, &a, &b, &w);
                    if (c2 != 1) MV_FAIL("J9. side %d, %s at write %d of the swap: %d here, %d there, bowl %d", side, mode ? "a refused write" : "the power cut", n, a, b, w);
                    if (side == 0 && c2 == 1 && !w) MV_FAIL("J9. side 0, write %d: the bowl's fish got into a tank by itself", n);
                }
        }
        /* MOVE's own save refused, then SWAP IN at once (its save refused too: no swap), then again */
        if (!two_tanks()) return 1;
        shelf_port_sim_fail(1); int r = progression_move_out(&T, 2); shelf_port_sim_fail(-1);
        shelf_port_sim_fail(0); bool sw = swap_to(0); shelf_port_sim_fail(-1);
        if (r != BOWL_OK || sw) MV_FAIL("J9. a swap started from a save that did not land (move %d, swap %d)", r, sw);
        if (!swap_to(0)) MV_FAIL("J9. the swap after it");
        { int a, b, w; if (copies(PIP, &a, &b, &w) != 1 || !w) MV_FAIL("J9. a refused save, then the swap: %d here, %d there, bowl %d", a, b, w); }
        printf("selftest-move: J4-J9 the erase owed, a shelf ID used again, a stage in the night of a cut, the swap cut and refused at every write (%d runs)\n", sruns);
    }

    { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", s_dir, s_pre); (void)system(cmd); }
    if (fails) return 1;
    printf("selftest-move: OK\n");
    return 0;
}

/* ---- the pictures (--bowl-shot <prefix>): each screen of the travel bowl as this glass draws it ---- */
static void mv_ppm(const char *prefix, const char *name) {
    char path[700]; snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t v = FB_SWAP16(fa[i]);
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
static void page_shot(const char *prefix, const char *name) {   /* the keeper page and whatever card is over it */
    for (int k = 0; k < 3; k++) { live(0.08f); render_frame_begin(); render_milestones(&T, fa, TANK_W); }
    mv_ppm(prefix, name);
}
static void tank_shot(const char *prefix, const char *name) {   /* the live tank and the card over it */
    for (int k = 0; k < 3; k++) { live(0.08f); render_frame_begin(); render_tank(&T, fa, TANK_W); render_give_modal(&T, fa, TANK_W); }
    mv_ppm(prefix, name);
}
int move_shot(const char *prefix) {
    board_env();
    static uint16_t scene[TANK_W * TANK_H], th[3][200 * 200];
    render_set_scene_cache(scene);
    if (!two_tanks()) { printf("bowl-shot: staging failed\n"); return 1; }
    while (T.n_fish < 4) tank_add_fish(&T, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    snprintf(T.fish[3].name, sizeof T.fish[3].name, "dot");
    live(8.0f);
    int tw, thh; render_shelf_thumb_size(&tw, &thh);
    for (int k = 0; k < 3; k++) render_tank(&T, fa, TANK_W);
    for (int k = 0; k < 3; k++) render_shelf_thumb(fa, th[k]);
    /* 1: the fish card with MOVE (RENAME | MOVE | SELL | MORE), and linked (GIVE too: five) */
    render_frame_begin(); render_milestones(&T, fa, TANK_W); render_milestones_show_fish(&T, 2);
    page_shot(prefix, "01_card_move");
    T.door_open[LINK_LEFT] = true; G.door_peer[LINK_LEFT] = 0x49439648u; G.door_badge[LINK_LEFT] = 1;
    page_shot(prefix, "02_card_move_linked");
    T.door_open[LINK_LEFT] = false;
    /* 2: PUT PIP IN THE TRAVEL BOWL? */
    render_move_tap(&T, 2); page_shot(prefix, "03_put_in_bowl");
    render_give_modal_close();
    /* 3: the refusals */
    T.fish[2].away = true; render_move_tap(&T, 2); page_shot(prefix, "06_why_next_door"); T.fish[2].away = false;
    G.asking_uid = PIP; render_move_tap(&T, 2); page_shot(prefix, "07_why_being_given"); G.asking_uid = 0;
    render_give_modal_close();
    progression_move_out(&T, 3);                      /* DOT is in the bowl */
    render_milestones_show_fish(&T, 2);
    page_shot(prefix, "08_card_move_dim");           /* (PIP's card: MOVE dim - the bowl is taken) */
    render_move_tap(&T, 2); page_shot(prefix, "05_why_bowl_has");
    render_give_modal_close();
    /* 4: the waiting row; MY TANKS's line */
    render_milestones_leave(); render_frame_begin(); render_milestones(&T, fa, TANK_W);
    page_shot(prefix, "09_waiting_row");
    {   shelf_slot_t s[8] = { { SHELF_NOW, "MY TANK", false, th[0] }, { SHELF_DORMANT, "TANK 2", false, th[1] }, { SHELF_PAUSED, "REEF", false, th[2] },
                              { SHELF_EMPTY, "", false, NULL }, { SHELF_EMPTY, "", false, NULL }, { SHELF_EMPTY, "", false, NULL }, { SHELF_EMPTY, "", false, NULL }, { SHELF_EMPTY, "", false, NULL } };
        render_frame_begin(); render_shelf(fa, TANK_W, s, 1, 1); mv_ppm(prefix, "10_my_tanks_line"); render_shelf_free(); }
    render_milestones_leave();
    /* 5: DOT IS WAITING - over the tank; refused: the tank full, the wrong habitat */
    render_bowl_card(&T); tank_shot(prefix, "11_is_waiting");
    progression_debug_bowl_type(7); tank_shot(prefix, "13_waiting_cant_live_here"); progression_debug_bowl_type(-1);
    render_give_modal_close();
    while (T.n_fish < POP_CAP) tank_add_fish(&T, 0, 1);
    while (progression_newborn() >= 0) progression_newborn_done(&T);
    live(4.0f);
    render_bowl_card(&T); tank_shot(prefix, "12_waiting_tank_full");
    render_give_modal_close();
    /* the last two; a fry not yet named */
    if (!two_tanks()) return 1;
    progression_move_out(&T, 2);
    render_frame_begin(); render_milestones(&T, fa, TANK_W); render_milestones_show_fish(&T, 0);
    render_move_tap(&T, 0); page_shot(prefix, "04a_why_last_two");
    render_give_modal_close();
    progression_bowl_let_in(&T);
    progression_force_arrival(&T);
    if (progression_newborn() >= 0) { render_milestones_show_fish(&T, progression_newborn()); render_move_tap(&T, progression_newborn()); page_shot(prefix, "04b_why_fry_not_named"); }
    render_give_modal_close(); render_milestones_leave();
    { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", s_dir, s_pre); (void)system(cmd); }
    printf("bowl-shot: wrote %s_*.ppm\n", prefix);
    return 0;
}
