#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_swap.c — FISH SWAP (0.4.3, common/swap.h, docs/fish-swap-0.4.3.md): its promises in the sim.
 *   ./fishsim --selftest-swap        (in make check)
 *   SWAP_SOAK=<seeds> for a longer soak, SWAP_SHOT=<prefix> to write the cards as PPMs
 *
 * 1. the book's blob: written, read back whole; a note that does not read was never there
 * 2. THE SOAK: two tanks of five, the real link engine over a hostile radio (drops, copies, delays, deaf spells, the link
 *    lost and made again), keepers who offer, pick, say no, call off, and crashes anywhere - at random, and inside the
 *    protocol between a note's save and the fish change it stands for. After every 10 ms: never two living copies of a
 *    fish; a fish alive nowhere is held by a note; never more than five fish and held places in a tank. At the end, the
 *    radio healed: every note settled, every fish alive exactly once, five and five.
 * 3. two real tanks through the cards: GIVE -> SWAP (their tank full), A SWAP FOR YOUR TANK -> PICK -> a fish -> SWAP,
 *    THEY PICKED -> SWAP: the fish trade places, keep their names and genes, their stages paid once; and each way it
 *    ends without a trade (NO THANKS, CALL OFF, the link gone before the trade, a 0.4.2 neighbor, a held fish), and the
 *    one hard case (the link gone in the moment after the trade, never back: a comes home after a day). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "progression.h"
#include "render.h"
#include "link.h"
#include "tank_link.h"
#include "gift.h"
#include "swap.h"
#include "advisor.h"

static int fails; static bool s_mute;   /* (the planted bugs: their failures are expected, not printed) */
#define SCHECK(cond, ...) do { if (!(cond)) { if (fails < 25 && !s_mute) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } fails++; } } while (0)

/* ---- a small RNG ---- */
static uint32_t s_rng = 1;
static uint32_t rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static float rndf(void) { return (rnd() & 0xffffff) / 16777216.0f; }
static uint32_t rndi(uint32_t a, uint32_t b) { return a + rnd() % (b - a + 1); }

static int nst(const swap_book_t *b) { for (int k = 0; k < SWAP_NOTES; k++) if (b->notes[k].st) return b->notes[k].st; return SWN_NONE; }   /* (a test's book: its one open note) */
static int nnotes(const swap_book_t *b) { int n = 0; for (int k = 0; k < SWAP_NOTES; k++) n += b->notes[k].st != SWN_NONE; return n; }
static swap_note_t *n1(swap_book_t *b) { for (int k = 0; k < SWAP_NOTES; k++) if (b->notes[k].st) return &b->notes[k]; return NULL; }
static passport_t mk_pp(uint32_t uid, uint32_t home, const char *name) {
    passport_t p; memset(&p, 0, sizeof p);
    p.uid = uid; p.home_tank = home; snprintf(p.name, sizeof p.name, "%s", name);
    p.stage = STAGE_ADULT; p.size = 1; p.bold = 0.4f; p.sociable = 0.6f; p.shape = 1; p.fins = 2; p.pattern = 1;
    p.color = 0xff8040; p.fin = 0xc04020; p.accent = 0xffffff; p.age_s = 7200; p.hunger = 3; p.energy = 7;
    return p;
}

/* =========================== 1. the blob =========================== */
static void part_blob(void) {
    static swap_book_t a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    swap_note_t *n = &a.notes[2];                        /* (any slot: the blob keeps the notes, not where they sat) */
    n->st = SWN_COMMITTED; n->role = SWAP_ROLE_I; n->applied = 1; n->sid = 77; n->peer = 0xB0B0u; n->tank = 0xA0A0u; n->wait_s = 1234u * 60u + 7u; n->wall_at = 1700000123u;
    n->out = mk_pp(0x111u, 0xA0A0u, "bolt"); n->in = mk_pp(0x222u, 0xB0B0u, "mira");
    for (int k = 0; k < 3; k++) { swap_note_t *m = &a.notes[k == 2 ? 3 : k]; *m = *n; m->st = SWN_DONE; m->role = SWAP_ROLE_R; m->sid = 100 + (uint32_t)k; m->tank = 0xC0u + (uint32_t)k; }
    a.seq = 4242;
    const swap_note_t n0 = *n;                           /* (n points into a.notes, zeroed below) */
    SCHECK(swap_save(&a), "blob: the save failed");
    const int full_len = a.mem_len;
    memcpy(b.mem, a.mem, sizeof a.mem); b.mem_len = a.mem_len; swap_load(&b);
    const swap_note_t *g = swap_note_of(&b, 0xA0A0u, 0);
    SCHECK(nnotes(&b) == 4 && g && g->st == SWN_COMMITTED && g->role == SWAP_ROLE_I && g->applied == 1 && g->sid == 77 && g->peer == 0xB0B0u && g->wait_s == 1234u * 60u + 7u && g->wall_at == 1700000123u
           && !strcmp(g->out.name, "bolt") && !strcmp(g->in.name, "mira") && g->in.uid == 0x222u && b.seq == 4242
           && swap_note_of(&b, 0xC1u, 0) && swap_note_of(&b, 0xC1u, 0)->sid == 101, "blob: the four notes did not read back whole (%d)", nnotes(&b));
    b.mem[4 + 26] ^= 0x5a; swap_load(&b);                /* a damaged passport in the first note: that note was never there, the rest were */
    SCHECK(nnotes(&b) == 3, "blob: a damaged note took %d of 4 with it (want 1)", 4 - nnotes(&b));
    memset(a.notes, 0, sizeof a.notes); swap_save(&a); memcpy(b.mem, a.mem, sizeof a.mem); b.mem_len = a.mem_len; swap_load(&b);
    SCHECK(nnotes(&b) == 0 && b.seq == 4242, "blob: no note - the count still reads back");
    {   /* a version 1 blob (11-21/22: flags in byte 3, then 8 finished entries) still reads: its notes, its count */
        uint8_t v1[700]; memset(v1, 0, sizeof v1);
        static swap_book_t c; memset(&c, 0, sizeof c); c.notes[0] = n0; swap_save(&c);   /* (a note's bytes, as the code writes them) */
        memcpy(v1, c.mem, 4 + 16); memcpy(v1 + 4 + 20, c.mem + 4 + 24, 258); v1[2] = 1; v1[3] = 1;   /* (version 1's note: a 20-byte head) */
        int p = 4 + 278; v1[p++] = 2; p += 18; v1[p] = 0x99; v1[p + 1] = 0x10;   /* (two finished entries, then the count 0x1099) */
        memcpy(b.mem, v1, sizeof v1); b.mem_len = p + 4; swap_load(&b);
        SCHECK(nnotes(&b) == 1 && b.notes[0].sid == 77 && b.seq == 0x1099u, "blob: a version 1 blob did not read (%d notes, seq %x)", nnotes(&b), b.seq);
    }
    SCHECK(full_len <= 1300, "blob: %d bytes", full_len);
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swap.sav", 1);
    static swap_book_t p, q; memset(&p, 0, sizeof p); memset(&q, 0, sizeof q); p.persist = q.persist = true;
    p.notes[0] = n0; p.notes[0].st = SWN_DONE; p.notes[0].role = SWAP_ROLE_R; p.notes[0].sid = 9; p.notes[0].peer = 5; p.notes[0].tank = 6;
    p.notes[0].out = mk_pp(0x333u, 6, "juno"); p.notes[0].in = mk_pp(0x444u, 5, "kelp");
    SCHECK(swap_save(&p) && swap_load(&q) && nst(&q) == SWN_DONE && !strcmp(n1(&q)->in.name, "kelp"), "blob: not read back from the flash");
    memset(p.notes, 0, sizeof p.notes); swap_save(&p);
    {   static swap_book_t r; memset(&r, 0, sizeof r);
        r.notes[0] = n0; r.notes[0].tank = 0xAAu; r.notes[1] = n0; r.notes[1].tank = 0xBBu; r.notes[1].sid = 4;
        swap_reset(&r, 0xBBu); SCHECK(nnotes(&r) == 1 && r.notes[0].tank == 0xAAu, "reset: a tank's reset took another tank's note");
        swap_reset(&r, 0xAAu); SCHECK(nnotes(&r) == 0, "reset: the tank's own reset (its old ID) left its note");
        r.notes[3] = n0; r.notes[3].tank = 0xCCu; swap_reset(&r, 0); SCHECK(nnotes(&r) == 0, "reset: ERASE EVERYTHING left a note");
        printf("selftest-swap: 1. a reset takes the notes of the tank reset (by its old ID); ERASE EVERYTHING takes every one\n"); }
    printf("selftest-swap: 1. the book's blob (%d bytes, four notes): read back whole; a damaged note was never there; version 1 still reads\n", full_len);
}
/* =========================== 2. the soak =========================== */
#define MF 8
typedef struct {
    uint32_t uid; link_t *L; swap_book_t B;
    passport_t f[MF]; int nf;                     /* the tank's fish - saved the moment they change (the notes are the risk) */
    uint32_t deaf_until;                          /* its radio hears nothing until then */
    uint32_t want_at, decide_at, answer_at, door_at, near_at;
    bool crashing;
    int8_t edge;                                  /* the door to the other tank */
} mt_t;
static mt_t M[2];
static uint32_t mnow;
typedef struct { int to; uint32_t at; int len; uint8_t b[LINK_FRAME_MAX]; } mframe_t;
static mframe_t mq[1024]; static int mqn;
static bool m_hostile = true, m_crashes = true, m_keepers = true;
static struct { int offers, swapped, ended, crashes, mid_crashes, aborts_seen, relinks; } ms;
static passport_t U[2 * 5];                        /* every fish there is */

static int m_of(void *ctx) { return (int)((mt_t *)ctx - M); }
static void m_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    int from = m_of(ctx);
    (void)to;
    int copies = 1;
    if (m_hostile) { float r = rndf(); if (r < 0.06f) copies = 0; else if (r < 0.09f) copies = 2; }
    for (int c = 0; c < copies && mqn < 1024; c++) {
        mframe_t *f = &mq[mqn++]; f->to = 1 - from; f->len = len; memcpy(f->b, buf, (size_t)len);
        f->at = mnow + (m_hostile ? rndi(0, 70) : 1);
    }
}
static int m_find(const mt_t *t, uint32_t uid) { for (int i = 0; i < t->nf; i++) if (t->f[i].uid == uid) return i; return -1; }
static void m_crash_now(mt_t *t);
static bool m_maybe_crash(mt_t *t) {             /* a crash inside the protocol, here: what follows in this call is lost */
    if (!m_crashes || t->crashing || rndf() > 0.03f) return false;
    t->crashing = true; t->B.fail_save = true; ms.mid_crashes++;
    return true;
}
static bool m_can_go(void *ctx, uint32_t uid, uint32_t peer) { (void)peer; return m_find(&M[m_of(ctx)], uid) >= 0; }
static bool m_settled(void *ctx, uint32_t uid, uint32_t peer) { (void)peer; return m_find(&M[m_of(ctx)], uid) >= 0 && (rnd() % 8) != 0; }   /* (now and then in a doorway: the trade waits, or R answers ABORT) */
static bool m_pp(void *ctx, uint32_t uid, passport_t *pp) { mt_t *t = &M[m_of(ctx)]; int i = m_find(t, uid); if (i < 0) return false; *pp = t->f[i]; return true; }
static bool m_out(void *ctx, uint32_t uid, bool bubbles) {
    (void)bubbles; mt_t *t = &M[m_of(ctx)];
    if (t->crashing) return false;
    bool crash = m_maybe_crash(t);
    if (!crash || rnd() & 1) { int i = m_find(t, uid); if (i >= 0) t->f[i] = t->f[--t->nf]; }
    return !crash;
}
static bool m_in(void *ctx, const passport_t *pp, int edge) {
    (void)edge; mt_t *t = &M[m_of(ctx)];
    if (t->crashing) return false;
    bool crash = m_maybe_crash(t);
    if (!crash || rnd() & 1) {
        if (m_find(t, pp->uid) < 0) { SCHECK(t->nf < 5, "soak t=%.1f s: tank %c took a sixth fish (%s)", mnow / 1000.0, 'A' + m_of(ctx), pp->name);
                                       if (t->nf < MF) t->f[t->nf++] = *pp; }
    }
    return !crash;
}
static bool m_at_door(void *ctx, uint32_t uid) { (void)uid; mt_t *t = &M[m_of(ctx)]; return t->door_at && (int32_t)(mnow - t->door_at) >= 0; }
static bool m_near_door(void *ctx, uint32_t uid) { (void)uid; return (rnd() & 3) != 0; }
static uint32_t m_hour(void *ctx) { (void)ctx; return 0; }   /* (no clock in the soak: the long wait never comes) */
static int64_t m_wall_now[2];                      /* (part 7: a wall clock; 0 = not set) */
static uint32_t m_gone_tank;                        /* (part 7: the tank the shelf no longer holds) */
static int64_t m_wall(void *ctx) { return m_wall_now[m_of(ctx)]; }
static bool m_gone(void *ctx, uint32_t tank) { (void)ctx; return tank && tank == m_gone_tank; }
static swap_host_t m_host(int i) { swap_host_t h = { &M[i], m_can_go, m_settled, m_pp, m_out, m_in, m_at_door, m_near_door, m_hour, NULL, NULL, NULL, NULL }; return h; }
static int m_home_away;                             /* (Q1 tests: our fish next door just now) */
static int m_home_now(void *ctx, uint32_t uid) { return m_find(&M[m_of(ctx)], uid) >= 0 ? (m_home_away ? -1 : 1) : 0; }
static bool m_room_f(void *ctx) { return M[m_of(ctx)].nf < 5; }
static swap_host_t m_hostw(int i) { swap_host_t h = m_host(i); h.wall = m_wall; h.tank_gone = m_gone; h.home_now = m_home_now; h.room = m_room_f; return h; }
static int m_room(void *ctx) { (void)ctx; return 2; }
static int m_fry_room(void *ctx) { (void)ctx; return 0; }
static bool m_ours(void *ctx, uint32_t uid) { return m_find(&M[m_of(ctx)], uid) >= 0; }
static bool m_full(void *ctx) { return M[m_of(ctx)].nf >= 5; }
static void m_link_up(int i) {
    mt_t *t = &M[i];
    link_host_t h = { t, m_send, m_room, NULL, m_fry_room, NULL, NULL, m_ours, m_full };
    link_init(t->L, t->uid, &h, rnd(), mnow);
    link_mode(t->L, true);
}
static void m_crash_now(mt_t *t) {
    int i = (int)(t - M);
    ms.crashes++;
    t->crashing = false; t->B.fail_save = false;
    swap_host_t h = m_host(i);
    swap_boot(&t->B, &h, t->uid);                 /* RAM gone, the book from its flash, what the note says finished */
    m_link_up(i);
    t->decide_at = t->answer_at = t->door_at = 0;
}
static bool m_note_holds(uint32_t uid) {
    for (int k = 0; k < 2; k++) for (int j = 0; j < SWAP_NOTES; j++) { const swap_note_t *n = &M[k].B.notes[j]; if (n->st != SWN_NONE && (n->out.uid == uid || n->in.uid == uid)) return true; }
    return false;
}
static bool m_here_v(const void *t, uint32_t uid) { return m_find((const mt_t *)t, uid) >= 0; }
static void m_check(void) {
    for (int u = 0; u < 10; u++) {
        int n = (m_find(&M[0], U[u].uid) >= 0) + (m_find(&M[1], U[u].uid) >= 0);
        SCHECK(n <= 1, "soak t=%.1f s: %s alive in both tanks", mnow / 1000.0, U[u].name);
        SCHECK(n == 1 || m_note_holds(U[u].uid), "soak t=%.1f s: %s alive nowhere, and no note holds it", mnow / 1000.0, U[u].name);
    }
    for (int k = 0; k < 2; k++) {
        int held = M[k].nf + swap_held_places(&M[k].B, M[k].uid, m_here_v, &M[k]);
        SCHECK(held <= 5, "soak t=%.1f s: tank %c holds %d (%d fish + places)", mnow / 1000.0, 'A' + k, held, M[k].nf);
    }
}
static void m_keeper(int i) {
    mt_t *t = &M[i]; swap_book_t *b = &t->B;
    uint32_t other = M[1 - i].uid;
    if (!m_keepers) { if (b->in.st == SI_ASKED) b->r_answer = -1; if (b->out.st == SO_PICKED) b->i_answer = -1; return; }
    if (!swap_busy(b) && t->nf && (rnd() % 400) == 0) { b->want_uid = t->f[rnd() % t->nf].uid; b->want_peer = other; ms.offers++; }
    if (b->out.st == SO_ASKING && (rnd() % 20000) == 0) b->i_cancel = true;
    if (b->in.st == SI_ASKED) {
        if (!t->decide_at) t->decide_at = mnow + (rndf() < 0.08f ? rndi(80000, 100000) : rndi(200, 15000));
        else if ((int32_t)(mnow - t->decide_at) >= 0) { t->decide_at = 0; if (rndf() < 0.15f || !t->nf) b->r_answer = -1; else b->r_pick = t->f[rnd() % t->nf].uid; }
    } else t->decide_at = 0;
    if (b->in.st == SI_WAITING && (rnd() % 30000) == 0) b->r_cancel = true;
    if (m_crashes && b->in.st == SI_GOING && (rnd() % 1500) == 0) { m_crash_now(t); return; }   /* (R down before the COMMIT: it answers ABORT, a comes home) */
    if (b->out.st == SO_PICKED) {
        if (!t->answer_at) t->answer_at = mnow + (rndf() < 0.05f ? rndi(55000, 70000) : rndi(100, 8000));
        else if ((int32_t)(mnow - t->answer_at) >= 0) { t->answer_at = 0; b->i_answer = rndf() < 0.85f ? 1 : -1; }
    } else t->answer_at = 0;
    if (b->out.st == SO_LEAVING) { if (!t->door_at) t->door_at = mnow + rndi(500, 30000); } else t->door_at = 0;
}
static void m_step(void) {
    mnow += 10;
    /* the radio */
    int m = 0;
    for (int k = 0; k < mqn; k++) {
        mframe_t f = mq[k];
        if ((int32_t)(f.at - mnow) > 0) { mq[m++] = f; continue; }
        mt_t *t = &M[f.to];
        if (t->deaf_until && (int32_t)(t->deaf_until - mnow) > 0) continue;
        link_rx(t->L, f.b, f.len, -40, mnow);
    }
    mqn = m;
    for (int i = 0; i < 2; i++) {
        mt_t *t = &M[i];
        if (m_hostile && (rnd() % 30000) == 0) t->deaf_until = mnow + rndi(500, 14000);   /* a deaf spell (past 10 s: the link is lost) */
        link_tick(t->L, mnow);
        if (!link_neighbor(t->L, (link_edge_t)t->edge) && (mnow % 700) == 0) { link_mode(t->L, true); link_arm(t->L, (link_edge_t)t->edge); }   /* (the keepers link again) */
        m_keeper(i);
        swap_host_t h = m_host(i);
        swap_tick(&t->B, t->L, &h, t->uid, mnow, mnow / 1000.0);
        if (t->crashing) m_crash_now(t);
        link_ev_t e;
        while (link_poll(t->L, &e)) {
            if (e.type == LEV_LINKED) ms.relinks++;
            if (e.type != LEV_SWAP) continue;
            if (e.reason == SWK_ABORT) ms.aborts_seen++;
            swap_rx(&t->B, t->L, &h, t->uid, e.peer, e.reason, e.nonce, e.uid, (int)e.amount, e.ms_left ? &e.pp : NULL, mnow, mnow / 1000.0);
            if (t->crashing) { m_crash_now(t); break; }
        }
        if (t->B.said.what == SWAP_SAID_SWAPPED && t->B.said.at > 0) { if (t->B.said.has_mine && t->B.said.mine.uid != t->B.said.theirs.uid) ms.swapped++; t->B.said.what = SWAP_SAID_NONE; }
        else if (t->B.said.what) { ms.ended++; t->B.said.what = SWAP_SAID_NONE; }
        if (m_crashes && (rnd() % 40000) == 0) m_crash_now(t);   /* a crash out of the blue */
    }
    m_check();
}
static void part_foreign(void) {
    /* (audit r1, LK-2 / LK-4 / LK-4R / LK-10 / PW-6 / PW-7) the book's notes and time. Tank Y on the glass; X's notes wait */
    memset(M, 0, sizeof M); m_crashes = false;          /* (no random crash inside these: the soak has them) */
    swap_book_t *fb = &M[0].B;
    swap_host_t h = m_host(0);                           /* (its clock: none - m_hour is 0) */
    swap_note_t ix; memset(&ix, 0, sizeof ix);
    ix.st = SWN_COMMITTED; ix.role = SWAP_ROLE_I; ix.applied = 1; ix.sid = 5; ix.peer = 7; ix.tank = 0xF00Du;
    ix.out = mk_pp(0x991u, 0xF00Du, "away"); ix.in = mk_pp(0x992u, 7, "other");
    fb->notes[0] = ix; swap_save(fb);
    swap_boot(fb, &h, 0xBEEFu);                          /* MY TANKS: tank BEEF on the glass */
    SCHECK(nnotes(fb) == 1 && !swap_busy(fb), "foreign: X's note blocks Y (busy %d)", swap_busy(fb));
    uint32_t t = 5000;
    for (int s2 = 0; s2 < 40 * 3600; s2++) { t += 1000; swap_tick(fb, NULL, &h, 0xBEEFu, t, s2); }
    SCHECK(nnotes(fb) == 1 && fb->notes[0].out.uid == 0x991u && fb->notes[0].st == SWN_COMMITTED, "foreign: X's note let go while it waited (LK-2)");
    /* every slot taken by other tanks' notes: Y is busy (no room for a note) - but nothing is ever let go to make room */
    for (int k = 1; k < SWAP_NOTES; k++) { fb->notes[k] = ix; fb->notes[k].tank = 0xA000u + (uint32_t)k; fb->notes[k].sid = 50 + (uint32_t)k; }
    SCHECK(swap_busy(fb), "foreign: four notes and still room");
    for (int s2 = 0; s2 < 30 * 3600; s2++) { t += 1000; swap_tick(fb, NULL, &h, 0xBEEFu, t, s2); }
    SCHECK(nnotes(fb) == SWAP_NOTES, "foreign: a note let go for room (%d left)", nnotes(fb));
    printf("selftest-swap: 1. other tanks' notes wait for their tanks (40 h, no clock): nothing let go, nothing blocked until all %d slots are full\n", SWAP_NOTES);
    /* the hard case at I, NO CLOCK (LK-4): a day of this board AWAKE with R not linked, then a comes home. Kept across a reboot */
    memset(M, 0, sizeof M); fb = &M[0].B; h = m_host(0);
    ix.tank = 0xBEEFu; fb->notes[1] = ix; M[0].uid = 0xBEEFu; swap_save(fb);
    swap_boot(fb, &h, 0xBEEFu); t = 5000;                /* (the engine's ms stand still from here: the board's radio is off with R gone - review 11-22) */
    int s2 = 1;
    for (; s2 < 12 * 3600; s2++) swap_tick(fb, NULL, &h, 0xBEEFu, t, s2);
    swap_crash(fb);
    SCHECK(nst(fb) == SWN_COMMITTED && n1(fb)->wait_s >= (12 * 60 - 10) * 60u, "no clock: %u minutes after a reboot at 12 h (want ~720)", n1(fb) ? n1(fb)->wait_s / 60 : 0);
    /* PW-6: a long sleep or the board off (a 25 h jump in the ms clock, a boot) counts NOTHING - only awake minutes do */
    swap_boot(fb, &h, 0xBEEFu); s2 += 25 * 3600; swap_tick(fb, NULL, &h, 0xBEEFu, t, s2);   /* (a day off: the clock jumps) */
    for (int k = 0; k < 600; k++) swap_tick(fb, NULL, &h, 0xBEEFu, t, ++s2);
    SCHECK(nst(fb) == SWN_COMMITTED, "PW-6: a sleep's jump counted toward the day (the note ended)");
    int awake = 12 * 3600 + 600;                         /* (awake seconds so far: the 25 h jump counted none) */
    for (; awake < 23 * 3600 + 1800; awake++) swap_tick(fb, NULL, &h, 0xBEEFu, t, ++s2);
    SCHECK(nst(fb) == SWN_COMMITTED, "no clock: let go before a day of awake minutes");
    for (; awake < 24 * 3600 + 1200; awake++) swap_tick(fb, NULL, &h, 0xBEEFu, t, ++s2);
    SCHECK(nst(fb) == SWN_GAVEUP && fb->said.what == SWAP_SAID_NOT_FINISHED && m_find(&M[0], 0x991u) >= 0, "LK-4: no clock, a day apart: a not home (note %d, said %d)", nst(fb), fb->said.what);
    printf("selftest-swap: 1. the hard case with no clock and the radio off (the engine's ms frozen): a day of the tank's awake minutes (kept across a reboot; a 25 h jump counts nothing), then a comes home\n");
    /* R's note, no clock (LK-4R / LK-10): it never lets go by itself - and it blocks nothing */
    memset(M, 0, sizeof M); fb = &M[0].B; h = m_host(0);
    swap_note_t rn = ix; rn.st = SWN_DONE; rn.role = SWAP_ROLE_R; rn.tank = 0xBEEFu; fb->notes[0] = rn; swap_save(fb);
    swap_boot(fb, &h, 0xBEEFu); t = 5000;
    for (int k = 0; k < 40 * 3600; k++) { t += 1000; swap_tick(fb, NULL, &h, 0xBEEFu, t, k); }
    SCHECK(nst(fb) == SWN_DONE && !swap_busy(fb), "LK-4R: R's note (st %d) let go, or R busy (%d)", nst(fb), swap_busy(fb));
    printf("selftest-swap: 1. R's note waits for I's CLOSED (40 h, no clock), and R is never BUSY for it\n");
    /* review 11-22: MY TANKS' REMOVE takes the removed tank's notes (every attached book); the bowl's check is this tank's notes that still move a fish */
    {   memset(M, 0, sizeof M); fb = &M[0].B;
        swap_attach((void *)0x1u, fb);
        fb->notes[0] = rn; fb->notes[0].tank = 0xD00Du; fb->notes[0].in = mk_pp(0x5550u, 7, "theirs"); fb->notes[1] = ix; fb->notes[1].tank = 0xBEEFu; fb->notes[2] = ix; fb->notes[2].tank = 0xD00Du; fb->notes[2].sid = 77;
        SCHECK(!swap_noted(fb, 0x5550u, 0xBEEFu) && swap_noted(fb, ix.in.uid, 0xBEEFu), "bowl: swap_noted reads another tank's notes, or misses this tank's");
        fb->notes[3] = rn; fb->notes[3].tank = 0xBEEFu; fb->notes[3].applied = 1; fb->notes[3].in = mk_pp(0x5551u, 7, "done");
        SCHECK(!swap_noted(fb, 0x5551u, 0xBEEFu), "bowl: an applied R note (moves nothing more) holds a fish");
        swap_forget_tank(0xD00Du);
        SCHECK(nnotes(fb) == 2 && fb->notes[1].tank == 0xBEEFu && fb->notes[3].tank == 0xBEEFu, "REMOVE: the removed tank's notes not forgotten (%d left)", nnotes(fb));
        swap_attach((void *)0x1u, NULL);
        printf("selftest-swap: 1. MY TANKS' REMOVE takes the removed tank's notes; the bowl waits only for this tank's notes that still move a fish\n"); }
}
static int part_soak_at(uint32_t seed, int minutes, bool quiet, uint32_t t0) {
    const int f0 = fails;
    s_rng = seed * 2654435761u + 7; if (!s_rng) s_rng = 1;
    memset(M, 0, sizeof M); mqn = 0; mnow = t0; memset(&ms, 0, sizeof ms);
    m_hostile = m_crashes = m_keepers = true;
    static const char *const NM[10] = { "bolt", "pip", "sunny", "dash", "fern", "mira", "kelp", "juno", "rio", "tiko" };
    for (int i = 0; i < 2; i++) {
        mt_t *t = &M[i];
        t->uid = 0x1000u + (uint32_t)i * 0x111u + seed;
        t->L = calloc(1, (size_t)link_size());
        t->edge = i == 0 ? LINK_RIGHT : LINK_LEFT;
        t->B.peer_edge = -1;
        for (int k = 0; k < 5; k++) { U[i * 5 + k] = mk_pp(0x7000u + seed * 16 + (uint32_t)(i * 5 + k), t->uid, NM[i * 5 + k]); t->f[t->nf++] = U[i * 5 + k]; }
        m_link_up(i);
    }
    for (int s = 0; s < 300; s++) { if (s == 10) link_arm(M[0].L, LINK_RIGHT); if (s == 20) link_arm(M[1].L, LINK_LEFT); m_step(); }
    SCHECK(link_neighbor(M[0].L, LINK_RIGHT) == M[1].uid, "soak: the two tanks never linked");
    for (int s = 0; s < minutes * 6000; s++) m_step();
    /* heal: a calm radio, no crashes, keepers who say no; everything settles */
    m_hostile = false; m_crashes = false; m_keepers = false;
    for (int i = 0; i < 2; i++) { M[i].deaf_until = 0; if (M[i].crashing) m_crash_now(&M[i]); }
    for (int s = 0; s < 6000 * 4; s++) m_step();
    for (int i = 0; i < 2; i++) {
        SCHECK(nst(&M[i].B) == SWN_NONE, "soak seed %u: tank %c still holds a note (state %d) after the radio healed", seed, 'A' + i, nst(&M[i].B));
        SCHECK(!swap_busy(&M[i].B), "soak seed %u: tank %c still busy (out %d, in %d)", seed, 'A' + i, M[i].B.out.st, M[i].B.in.st);
        SCHECK(M[i].nf == 5, "soak seed %u: tank %c ends with %d fish", seed, 'A' + i, M[i].nf);
    }
    for (int u = 0; u < 10; u++) {
        int n = (m_find(&M[0], U[u].uid) >= 0) + (m_find(&M[1], U[u].uid) >= 0);
        SCHECK(n == 1, "soak seed %u: %s alive in %d tanks at the end", seed, U[u].name, n);
    }
    if (!s_mute && (!quiet || fails != f0))
        printf("selftest-swap: 2. soak seed %u, %d min%s: %d offers, %d swapped, %d ended otherwise, %d crashes (%d inside the protocol), %d ABORTs, %d links%s\n",
               seed, minutes, t0 > 0x70000000u ? " (the ms clock across 2^31)" : "", ms.offers, ms.swapped, ms.ended, ms.crashes, ms.mid_crashes, ms.aborts_seen, ms.relinks, fails != f0 ? " - FAILED" : "");
    for (int i = 0; i < 2; i++) { free(M[i].L); M[i].L = NULL; }
    return fails - f0;
}
static int part_soak(uint32_t seed, int minutes, bool quiet) { return part_soak_at(seed, minutes, quiet, 1000); }

/* =========================== 3. two real tanks through the cards =========================== */
typedef struct { int to, len; uint8_t b[LINK_FRAME_MAX]; } rframe_t;
static rframe_t rq[512]; static int rqn;
static tank_t RA, RB; static link_t *LA, *LB;
static gift_book_t GA, GB; static swap_book_t SA, SB;
static uint32_t rnow;
static bool r_deaf[2]; static bool r_drop_swap_to[2]; static int r_drop_kind_to[2];   /* (a swap frame of that kind to that tank lost) */ static bool r_double_ok;   /* (3f: the rare double, by design) */
static uint32_t r_hour_now;
static uint32_t r_hour(void) { return r_hour_now; }
/* (audit r1, LK) a hostile radio for the real tanks: frames lost, copied, held back and so reordered */
static bool c_chaos; static float c_loss = 0.10f, c_dup = 0.04f;
typedef struct { int to, len, due; uint8_t b[LINK_FRAME_MAX]; } cframe_t;
static cframe_t cq[2048]; static int cqn;
static void r_send(void *ctx, uint32_t to, const uint8_t *buf, int len) {
    int from = ctx == &RA ? 0 : 1;
    if (c_chaos) {
        if (to && to != (from ? RA.uid : RB.uid)) return;
        float r = rndf(); int copies = r < c_loss ? 0 : r < c_loss + c_dup ? 2 : 1;
        for (int c = 0; c < copies && cqn < 2048; c++) { cq[cqn].to = 1 - from; cq[cqn].len = len; cq[cqn].due = (int)rndi(0, 4); memcpy(cq[cqn].b, buf, (size_t)len); cqn++; }
        return;
    }
    if (r_drop_swap_to[1 - from] && link_frame_is_swap(buf, len)) return;
    if (r_drop_kind_to[1 - from] && link_frame_is_swap(buf, len) && len > 12 && buf[12] == r_drop_kind_to[1 - from]) return;
    if (rqn < 512 && (!to || to == (from ? RA.uid : RB.uid))) { rq[rqn].to = 1 - from; rq[rqn].len = len; memcpy(rq[rqn].b, buf, (size_t)len); rqn++; }
}
static int r_vis(const tank_t *t, uint32_t uid) { for (int k = 0; k < N_VISIT_MAX; k++) if (t->vis_used[k] && t->fish[N_FISH_MAX + k].uid == uid) return N_FISH_MAX + k; return -1; }
static void r_step(int frames) {
    for (int n = 0; n < frames; n++) {
        rnow += 33;
        int m = rqn; static rframe_t q[512]; memcpy(q, rq, sizeof(rframe_t) * (size_t)m); rqn = 0;
        for (int i = 0; i < m; i++) if (!r_deaf[q[i].to]) link_rx(q[i].to ? LB : LA, q[i].b, q[i].len, -30, rnow);
        if (c_chaos && cqn) {                           /* (LK) the due frames, in a shuffled order */
            static cframe_t due[2048]; int nd = 0, keep = 0;
            for (int i = 0; i < cqn; i++) { if (cq[i].due-- <= 0) due[nd++] = cq[i]; else cq[keep++] = cq[i]; }
            cqn = keep;
            for (int i = nd - 1; i > 0; i--) { int j = (int)(rnd() % (uint32_t)(i + 1)); cframe_t tt = due[i]; due[i] = due[j]; due[j] = tt; }
            for (int i = 0; i < nd; i++) if (!r_deaf[due[i].to]) link_rx(due[i].to ? LB : LA, due[i].b, due[i].len, -30, rnow);
        }
        tank_t *T2[2] = { &RA, &RB }; link_t *L2[2] = { LA, LB };
        for (int k = 0; k < 2; k++) {
            tank_tick(T2[k], 0.033f, NULL);
            link_tick(L2[k], rnow);
            tank_link_tick(T2[k], L2[k]);
            link_ev_t e; while (link_poll(L2[k], &e)) tank_link_event(T2[k], L2[k], &e);
        }
        if (!r_double_ok) {                              /* never simulated twice: home (not away) in one tank and a visitor in the other */
            for (int i = 0; i < RA.n_fish; i++) SCHECK(RA.fish[i].away || r_vis(&RB, RA.fish[i].uid) < 0, "cards t=%.1f: %s swims home in A and visits B", rnow / 1000.0, RA.fish[i].name);
            for (int i = 0; i < RB.n_fish; i++) SCHECK(RB.fish[i].away || r_vis(&RA, RB.fish[i].uid) < 0, "cards t=%.1f: %s swims home in B and visits A", rnow / 1000.0, RB.fish[i].name);
        }
        if (!r_double_ok) for (int i = 0; i < RA.n_fish; i++) for (int j = 0; j < RB.n_fish; j++) SCHECK(RA.fish[i].uid != RB.fish[j].uid, "cards t=%.1f: %s is in both tanks", rnow / 1000.0, RA.fish[i].name);
        SCHECK(progression_fish_held(&RA) <= POP_CAP && progression_fish_held(&RB) <= POP_CAP, "cards t=%.1f: past five (A %d, B %d)", rnow / 1000.0, progression_fish_held(&RA), progression_fish_held(&RB));
    }
}
static int r_slot(const tank_t *t, uint32_t uid) { for (int i = 0; i < t->n_fish; i++) if (t->fish[i].uid == uid) return i; return -1; }
static uint16_t r_fb[TANK_W * TANK_H];
static void r_shot(const tank_t *t, const char *name) {
    render_frame_begin(); render_tank(t, r_fb, TANK_W); render_gift(t, r_fb, TANK_W);
    const char *pfx = getenv("SWAP_SHOT"); if (!pfx) return;
    char path[512]; snprintf(path, sizeof path, "%s_%s.ppm", pfx, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int q = 0; q < TANK_W * TANK_H; q++) { uint16_t v = FB_SWAP16(r_fb[q]);
        unsigned char rgb[3] = { (unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3) }; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
/* a tap on the card's button, as a keeper's: the card seen first (drawn), a moment, then the tap */
static int r_tap(tank_t *t, int which) {
    render_frame_begin(); render_tank(t, r_fb, TANK_W); render_gift(t, r_fb, TANK_W);
    r_step(40);
    render_frame_begin(); render_tank(t, r_fb, TANK_W); render_gift(t, r_fb, TANK_W);
    int x, y;
    if (!render_swap_button(t, which, &x, &y)) return -1;
    return render_swap_tap(t, (float)x, (float)y);
}
static void r_fill(tank_t *t, uint32_t seed) {
    tank_init(t, seed); tank_new_population(t);
    while (t->n_fish < POP_CAP) { tank_make_fish(t, t->n_fish, t->n_fish % 4, 0.5f, 0.5f, STAGE_ADULT); t->n_fish++; }
    tank_ensure_ids(t);
    for (int i = 0; i < t->n_fish; i++) { progression_set_age(t, i, STAGE_ADULT_AGE + 60); t->fish[i].stage = STAGE_ADULT; t->fish[i].ms_bits |= MS_REACHED_JUV | MS_REACHED_ADULT; t->fish[i].door_seen = 1e9; }
}
static void r_setup(bool b_old) {
    rnow = 1000; rqn = 0; r_deaf[0] = r_deaf[1] = false; r_drop_swap_to[0] = r_drop_swap_to[1] = false; r_drop_kind_to[0] = r_drop_kind_to[1] = 0;
    memset(&GA, 0, sizeof GA); memset(&GB, 0, sizeof GB); memset(&SA, 0, sizeof SA); memset(&SB, 0, sizeof SB); SA.peer_edge = SB.peer_edge = -1;
    r_fill(&RA, 81); r_fill(&RB, 82);
    static const char *const NA[5] = { "bolt", "pip", "sunny", "dash", "fern" }, *const NB[5] = { "mira", "kelp", "juno", "rio", "tiko" };
    for (int i = 0; i < 5; i++) { snprintf(RA.fish[i].name, sizeof RA.fish[i].name, "%s", NA[i]); snprintf(RB.fish[i].name, sizeof RB.fish[i].name, "%s", NB[i]); }
    gift_attach(&RA, &GA); gift_attach(&RB, &GB); swap_attach(&RA, &SA); swap_attach(&RB, &SB);
    if (!LA) { LA = calloc(1, (size_t)link_size()); LB = calloc(1, (size_t)link_size()); }
    link_host_t ha = tank_link_host(&RA, r_send), hb = tank_link_host(&RB, r_send);
    link_init(LA, RA.uid, &ha, 1, rnow); link_init(LB, RB.uid, &hb, 2, rnow);
    link_test_as_042(LB, b_old);
    link_mode(LA, true); link_mode(LB, true); r_step(10);
    link_arm(LA, LINK_RIGHT); r_step(5); link_arm(LB, LINK_LEFT); r_step(60);
    SCHECK(RA.door_open[LINK_RIGHT] && RB.door_open[LINK_LEFT], "cards: the doors did not open");
    render_swap_forget(); render_give_modal_close();
    for (int i = 0; i < POP_CAP; i++) { RA.fish[i].door_seen = RA.clock + 1e6; RB.fish[i].door_seen = RB.clock + 1e6; }
}
static void r_down(void) { gift_attach(&RA, NULL); gift_attach(&RB, NULL); swap_attach(&RA, NULL); swap_attach(&RB, NULL); render_swap_forget(); render_give_modal_close(); }
/* A's keeper: GIVE on fish 0, then SWAP on the card (their tank is full) */
static bool r_offer(int fish) {
    render_give_tap(&RA, fish);
    render_frame_begin(); render_tank(&RA, r_fb, TANK_W); render_give_modal(&RA, r_fb, TANK_W);
    r_step(40);
    render_frame_begin(); render_tank(&RA, r_fb, TANK_W); render_give_modal(&RA, r_fb, TANK_W);
    if (render_give_modal_step(&RA) != RENDER_GM_CONFIRM) return false;
    int x, y; if (!render_give_modal_button(&RA, RENDER_GM_TAP_GIVE, &x, &y)) return false;
    return render_give_modal_tap(&RA, (float)x, (float)y) == RENDER_GM_TAP_GIVE;
}
/* one of ours to the tank next door, as a visitor (the doors harness's walk); -1 = it never got there */
static int r_visit(tank_t *home, int i, int edge, tank_t *host) {
    uint32_t u = home->fish[i].uid;
    fish_t *f = &home->fish[i];
    f->goal.id = GOAL_EXPLORE; f->door = DOOR_HEADING; f->door_edge = (int8_t)edge; f->door_refused = 0;
    for (int n = 0; n < 1800 * ((TANK_W + 447) / 448); n++) { r_step(1); if (r_vis(host, u) >= 0 && home->fish[i].away) return n; }
    return -1;
}
static void r_stay(tank_t *host, uint32_t u) { int v = r_vis(host, u); if (v >= 0) { host->fish[v].homeward = false; host->fish[v].bolt_home = false; } }
/* the whole swap through the cards: A offers its fish ia, B picks its fish ib; true = it came to the last yes */
static bool r_through(int ia, int ib) {
    if (!r_offer(ia)) return false;
    r_step(20);
    if (r_tap(&RB, RENDER_SW_BTN_LEFT) != RENDER_SW_TAP_YES) return false;
    if (r_tap(&RB, RENDER_SW_BTN_TILE0 + ib) != RENDER_SW_TAP_FISH) return false;
    if (r_tap(&RB, RENDER_SW_BTN_LEFT) != RENDER_SW_TAP_YES) return false;
    r_step(20);
    return SA.out.st == SO_PICKED;
}
static int r_settle(uint32_t a, uint32_t b) {      /* steps until a is B's and b is A's, both notes gone (-1: never) */
    for (int n = 0; n < 1500; n++) {
        if (r_slot(&RA, b) >= 0 && r_slot(&RB, a) >= 0 && nst(&SA) == SWN_NONE && nst(&SB) == SWN_NONE && SA.out.st == SO_OFF && SB.in.st == SI_OFF) return n;
        r_step(1);
    }
    return -1;
}
static void part_visiting(void) {
    /* 3i. A's fish VISITING B is offered (Strato 11-22): it stays where it swims, B's own now; B's fish swims to A */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a = RA.fish[0].uid;
      SCHECK(r_visit(&RA, 0, LINK_RIGHT, &RB) >= 0, "3i: A's fish never got to B");
      r_stay(&RB, a);
      SCHECK(r_offer(0) && render_give_modal_step(&RA) == RENDER_GM_OFF, "3i: SWAP not offered for a fish visiting the partner");
      r_stay(&RB, a); r_step(20);
      SCHECK(SA.out.st == SO_ASKING, "3i: no ask (out %d, said %d)", SA.out.st, SA.said.what);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 2); r_tap(&RB, RENDER_SW_BTN_LEFT); r_stay(&RB, a); r_step(20);
      uint32_t b = SA.out.b.uid;
      int v0 = r_vis(&RB, a); float x0 = v0 >= 0 ? RB.fish[v0].x : -1;
      r_stay(&RB, a); r_tap(&RA, RENDER_SW_BTN_LEFT);
      int n = r_settle(a, b);
      int ia = r_slot(&RB, a);
      SCHECK(n >= 0 && ia >= 0 && !RB.fish[ia].away && r_vis(&RB, a) < 0 && r_slot(&RA, a) < 0, "3i: not traded (%d; a in B %d, still a visitor %d, in A %d)", n, ia, r_vis(&RB, a), r_slot(&RA, a));
      SCHECK(RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3i: %d / %d", RA.n_fish, RB.n_fish);
      SCHECK(ia < 0 || x0 < 0 || fabsf(RB.fish[ia].x - x0) < 120, "3i: it did not stay where it swam (x %.0f, then %.0f)", x0, ia >= 0 ? RB.fish[ia].x : -1);
      r_step(400);                                   /* (its old lease's time passes: nothing comes back to A, nothing poofs at B) */
      SCHECK(r_slot(&RA, a) < 0 && r_slot(&RB, a) >= 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3i: 13 s later: a in A %d, in B %d", r_slot(&RA, a), r_slot(&RB, a));
      if (fails == f1) printf("selftest-swap: 3i. a fish visiting the partner, swapped: it stays where it swims as theirs, the other swims over; nothing comes back after\n");
      r_down(); }

    /* 3j. B picks its fish VISITING A: it stays in A as A's */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t b = RB.fish[1].uid, a = RA.fish[0].uid;
      SCHECK(r_visit(&RB, 1, LINK_LEFT, &RA) >= 0, "3j: B's fish never got to A");
      r_stay(&RA, b);
      SCHECK(r_offer(0), "3j: no SWAP"); r_stay(&RA, b); r_step(20);
      SCHECK(r_tap(&RB, RENDER_SW_BTN_LEFT) == RENDER_SW_TAP_YES && r_tap(&RB, RENDER_SW_BTN_TILE0 + 1) == RENDER_SW_TAP_FISH, "3j: B's visiting fish's tile did not answer (dim?)");
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_stay(&RA, b); r_step(20);
      r_stay(&RA, b); r_tap(&RA, RENDER_SW_BTN_LEFT);
      int n = r_settle(a, b);
      int ib = r_slot(&RA, b);
      SCHECK(n >= 0 && ib >= 0 && !RA.fish[ib].away && r_vis(&RA, b) < 0 && r_slot(&RB, b) < 0, "3j: not traded (%d; b in A %d, visitor %d, in B %d)", n, ib, r_vis(&RA, b), r_slot(&RB, b));
      r_step(400);
      SCHECK(r_slot(&RB, b) < 0 && r_slot(&RA, b) >= 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3j: 13 s later: b in A %d, in B %d", r_slot(&RA, b), r_slot(&RB, b));
      if (fails == f1) printf("selftest-swap: 3j. B's fish visiting A, picked: it stays in A as A's; A's fish swims over\n");
      r_down(); }

    /* 3k. both visiting each other: the trade happens where they are, nothing swims */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a = RA.fish[0].uid, b = RB.fish[1].uid;
      SCHECK(r_visit(&RA, 0, LINK_RIGHT, &RB) >= 0 && r_visit(&RB, 1, LINK_LEFT, &RA) >= 0, "3k: the visits never happened");
      r_stay(&RB, a); r_stay(&RA, b);
      SCHECK(r_offer(0), "3k: no SWAP"); r_stay(&RB, a); r_stay(&RA, b); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 1); r_tap(&RB, RENDER_SW_BTN_LEFT); r_stay(&RB, a); r_stay(&RA, b); r_step(20);
      r_stay(&RB, a); r_stay(&RA, b); r_tap(&RA, RENDER_SW_BTN_LEFT);
      int n = r_settle(a, b);
      SCHECK(n >= 0 && r_vis(&RA, b) < 0 && r_vis(&RB, a) < 0, "3k: not traded where they were (%d)", n);
      r_step(400);
      SCHECK(r_slot(&RA, b) >= 0 && r_slot(&RB, a) >= 0 && r_slot(&RA, a) < 0 && r_slot(&RB, b) < 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3k: 13 s later the trade did not hold");
      if (fails == f1) printf("selftest-swap: 3k. both visiting each other: traded where they swim (%.1f s), nothing swims, nothing comes back\n", n * 0.033);
      r_down(); }

    /* 3l. the visit ends by itself mid-offer: A's fish comes home before the last yes - it swims over through the door */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a = RA.fish[0].uid;
      SCHECK(r_visit(&RA, 0, LINK_RIGHT, &RB) >= 0, "3l: never got to B");
      r_stay(&RB, a);
      SCHECK(r_offer(0), "3l: no SWAP"); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 3); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
      uint32_t b = SA.out.b.uid;
      int v = r_vis(&RB, a); if (v >= 0) { RB.fish[v].homeward = true; RB.fish[v].bolt_home = true; }   /* (its visit is over: home it swims) */
      int k = 0; for (; k < 900 && (r_slot(&RA, a) < 0 || RA.fish[r_slot(&RA, a)].away); k++) r_step(1);
      SCHECK(r_slot(&RA, a) >= 0 && !RA.fish[r_slot(&RA, a)].away, "3l: it never came home");
      SCHECK(SA.out.st == SO_PICKED, "3l: the swap ended when the visit did (out %d)", SA.out.st);
      r_tap(&RA, RENDER_SW_BTN_LEFT);
      int n = r_settle(a, b);
      SCHECK(n >= 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3l: no trade after it came home (%d)", n);
      if (fails == f1) printf("selftest-swap: 3l. the visit ended mid-offer: it came home, the swap went on, through the door\n");
      r_down(); }

    /* 3m. the trade's frame lost with A's fish visiting B: B lets the visitor go (no lease, DROP), B's pick runs out - A's
       COMMIT, sent again, is answered ABORT; A's fish comes home from its note. Never in two places, never in none */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a = RA.fish[0].uid;
      SCHECK(r_visit(&RA, 0, LINK_RIGHT, &RB) >= 0, "3m: never got to B");
      r_stay(&RB, a);
      SCHECK(r_offer(0), "3m: no SWAP"); r_stay(&RB, a); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 2); r_tap(&RB, RENDER_SW_BTN_LEFT); r_stay(&RB, a); r_step(20);
      r_drop_swap_to[1] = true;                          /* nothing of the swap reaches B: not GO, not COMMIT */
      r_stay(&RB, a); r_tap(&RA, RENDER_SW_BTN_LEFT);
      int k = 0; for (; k < 600 && nst(&SA) != SWN_COMMITTED; k++) { r_stay(&RB, a); r_step(1); }
      SCHECK(nst(&SA) == SWN_COMMITTED && r_slot(&RA, a) < 0, "3m: staging (note %d)", nst(&SA));
      for (k = 0; k < (SWAP_PICK_TTL_MS + 20000) / 33; k++) {
          r_step(1);
          SCHECK(r_slot(&RA, a) < 0 || r_vis(&RB, a) < 0, "3m: a home in A and a visitor in B at once");
          if (fails > f1) break;
      }
      SCHECK(r_vis(&RB, a) < 0 && SB.in.st == SI_OFF, "3m: B still holds the visitor (%d) or the pick (%d)", r_vis(&RB, a), SB.in.st);
      r_drop_swap_to[1] = false;
      for (k = 0; k < 300 && nst(&SA) != SWN_NONE; k++) r_step(1);
      SCHECK(nst(&SA) == SWN_NONE && r_slot(&RA, a) >= 0 && r_slot(&RB, a) < 0 && SA.said.what == SWAP_SAID_NOT_HAPPENED && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP,
             "3m: not settled (note %d, a in A %d, in B %d, said %d)", nst(&SA), r_slot(&RA, a), r_slot(&RB, a), SA.said.what);
      if (fails == f1) printf("selftest-swap: 3m. the trade's frame lost with the fish visiting: B let it go, ABORT, home from its note - never two, never none\n");
      r_down(); }

    /* 3o. B cannot take the trade the moment it comes (its pick gone: a fish it can no longer part with), A's fish visiting B
       (the review's case, 11-22): B sends A's fish to bubbles BEFORE its ABORT - A brings it home and it never swims in both */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a = RA.fish[0].uid;
      SCHECK(r_visit(&RA, 0, LINK_RIGHT, &RB) >= 0, "3o: never got to B");
      r_stay(&RB, a);
      SCHECK(r_offer(0), "3o: no SWAP"); r_stay(&RB, a); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 2); r_tap(&RB, RENDER_SW_BTN_LEFT); r_stay(&RB, a); r_step(20);
      int jb = r_slot(&RB, SA.out.b.uid);
      r_drop_kind_to[0] = SWK_NO;                        /* B's NO about it never reaches A: A commits, B must answer ABORT */
      if (jb >= 0) RB.fish[jb].born_away = true;         /* (B can no longer part with its pick) */
      r_stay(&RB, a); r_tap(&RA, RENDER_SW_BTN_LEFT);
      int k = 0; for (; k < 600 && !(nst(&SA) == SWN_NONE && SA.said.what == SWAP_SAID_NOT_HAPPENED); k++) r_step(1);
      if (jb >= 0) RB.fish[jb].born_away = false;
      r_drop_kind_to[0] = 0; r_step(200);
      SCHECK(nst(&SA) == SWN_NONE && r_slot(&RA, a) >= 0 && r_vis(&RB, a) < 0 && r_slot(&RB, a) < 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP,
             "3o: not as it was (note %d, a in A %d, visitor in B %d, said %d)", nst(&SA), r_slot(&RA, a), r_vis(&RB, a), SA.said.what);
      if (fails == f1) printf("selftest-swap: 3o. B could not take the trade with A's fish visiting it: B sent it to bubbles before its ABORT - home at A, never in both\n");
      r_down(); }

    /* 3n. B's DONE lost with B's fish visiting A: B traded it; A's visitor is DROPped (B has let it go) - A brings it in
       from its note when DONE comes again. Never in two places */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t b = RB.fish[1].uid, a = RA.fish[0].uid;
      SCHECK(r_visit(&RB, 1, LINK_LEFT, &RA) >= 0, "3n: never got to A");
      r_stay(&RA, b);
      SCHECK(r_offer(0), "3n: no SWAP"); r_stay(&RA, b); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 1); r_tap(&RB, RENDER_SW_BTN_LEFT); r_stay(&RA, b); r_step(20);
      r_drop_swap_to[0] = true;                          /* B's DONE never reaches A */
      r_stay(&RA, b); r_tap(&RA, RENDER_SW_BTN_LEFT);
      int k = 0; for (; k < 1500 && nst(&SB) != SWN_DONE; k++) { r_stay(&RA, b); r_step(1); }
      SCHECK(nst(&SB) == SWN_DONE && r_slot(&RB, b) < 0, "3n: staging (B note %d)", nst(&SB));
      for (k = 0; k < 450; k++) { r_step(1); SCHECK(r_slot(&RB, b) < 0, "3n: b back in B"); if (fails > f1) break; }
      r_drop_swap_to[0] = false;
      int n = r_settle(a, b);
      SCHECK(n >= 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3n: not settled (%d)", n);
      if (fails == f1) printf("selftest-swap: 3n. DONE lost with B's fish visiting A: it waited in the notes, came in at A once DONE came again\n");
      r_down(); }
}
static void part_cards(void) {
    const int f0 = fails;
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swapcards.sav", 1);
    tank_link_swap_hour = r_hour; r_hour_now = 500000;
    /* 3a. the whole swap */
    r_setup(false);
    r_step(40);
    SCHECK(SA.door_full[LINK_RIGHT] && SA.door_swap[LINK_RIGHT], "3a: A does not see B as full and taking swaps (full %d, swap %d)", SA.door_full[LINK_RIGHT], SA.door_swap[LINK_RIGHT]);
    uint32_t a = RA.fish[0].uid, bfish = RB.fish[2].uid;
    uint32_t a_shape = RA.fish[0].shape, a_color = RA.fish[0].color;
    RA.fish[0].stage = STAGE_ELDER; RA.fish[0].evo = 3; RA.fish[0].evo_offer = 0;   /* (a PARAGON wearing part 3: it must wear it next door) */
    const uint8_t a_evo = RA.fish[0].evo;
    RA.fish[0].body[0] = 190; RA.fish[0].body[1] = 90; RA.fish[0].body_c[0] = 180; RA.fish[0].body_c[1] = 100;   /* (and a LAB fish's own body) */
    RA.fish[0].finl[0] = 131; RA.fish[0].finl[1] = 81; RA.fish[0].finl[2] = 111; RA.fish[0].finl_c[0] = RA.fish[0].finl_c[1] = RA.fish[0].finl_c[2] = 101;
    const uint32_t a_shape2 = RA.fish[0].shape; (void)a_shape2;
    SCHECK(r_offer(0), "3a: the GIVE card did not offer SWAP (step %d)", render_give_modal_step(&RA));
    r_step(20);
    SCHECK(render_swap_card(&RA) == 1 /* I ASKING */, "3a: A's card is not ASKING THEIR KEEPER (%d)", render_swap_card(&RA));
    SCHECK(SB.in.st == SI_ASKED && SB.in.a.uid == a, "3a: the offer never reached B");
    r_shot(&RA, "1_I_asking"); r_shot(&RB, "2_R_offer");
    SCHECK(r_tap(&RB, RENDER_SW_BTN_LEFT) == RENDER_SW_TAP_YES, "3a: PICK did not answer");
    r_shot(&RB, "3_R_tiles");
    SCHECK(r_tap(&RB, RENDER_SW_BTN_TILE0 + 2) == RENDER_SW_TAP_FISH, "3a: a fish tile did not answer");
    r_shot(&RB, "4_R_confirm");
    SCHECK(r_tap(&RB, RENDER_SW_BTN_LEFT) == RENDER_SW_TAP_YES, "3a: B's SWAP did not answer");
    r_step(20);
    SCHECK(SB.in.st == SI_WAITING && SA.out.st == SO_PICKED && SA.out.b.uid == bfish, "3a: the pick never reached A (B %d, A %d)", SB.in.st, SA.out.st);
    SCHECK(!progression_fish_sellable(&RB, r_slot(&RB, bfish)), "3a: B's picked fish can be sold while it waits");
    SCHECK(!progression_fish_sellable(&RA, r_slot(&RA, a)), "3a: A's offered fish can be sold while it waits");
    r_shot(&RB, "5_R_waiting"); r_shot(&RA, "6_I_last_yes");
    SCHECK(r_tap(&RA, RENDER_SW_BTN_LEFT) == RENDER_SW_TAP_YES, "3a: A's last SWAP did not answer");
    int n = 0;
    for (; n < 1500 && !(r_slot(&RA, bfish) >= 0 && r_slot(&RB, a) >= 0 && nst(&SA) == SWN_NONE && nst(&SB) == SWN_NONE); n++) r_step(1);
    int ia = r_slot(&RB, a), ib = r_slot(&RA, bfish);
    SCHECK(ia >= 0 && ib >= 0 && r_slot(&RA, a) < 0 && r_slot(&RB, bfish) < 0, "3a: no trade after %.1f s (a in B %d, b in A %d)", n * 0.033, ia, ib);
    SCHECK(RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3a: %d / %d fish after the trade", RA.n_fish, RB.n_fish);
    if (ia >= 0) {
        SCHECK(!strcmp(RB.fish[ia].name, "bolt") && RB.fish[ia].shape == a_shape && RB.fish[ia].color == a_color && RB.fish[ia].stage == STAGE_ELDER,
               "3a: BOLT arrived changed (%s, shape %u, stage %d)", RB.fish[ia].name, (unsigned)RB.fish[ia].shape, RB.fish[ia].stage);
        SCHECK(RB.fish[ia].body[0] == 190 && RB.fish[ia].body[1] == 90 && RB.fish[ia].body_c[0] == 180 && RB.fish[ia].finl[0] == 131 && RB.fish[ia].finl[1] == 81
               && RB.fish[ia].finl[2] == 111 && RB.fish[ia].finl_c[2] == 101, "3a: the lab fish's body did not cross (body %u/%u, fins %u/%u/%u)",
               RB.fish[ia].body[0], RB.fish[ia].body[1], RB.fish[ia].finl[0], RB.fish[ia].finl[1], RB.fish[ia].finl[2]);
        SCHECK(RB.fish[ia].evo == a_evo, "3a: the PARAGON's part did not cross (wore %d, arrived wearing %d)", a_evo, RB.fish[ia].evo);
        SCHECK((RB.sd_paid_fish[ia] & SD_PAID_ADULT) && (RB.sd_paid_fish[ia] & SD_PAID_TRUST), "3a: B will pay BOLT's stage again (paid %x)", RB.sd_paid_fish[ia]);
        SCHECK(SB.said.has_theirs && SB.said.theirs.home_tank == RA.uid, "3a: BOLT came without A's waters on its passport (ITS STORY's FROM)");
    }
    SCHECK(render_swap_card(&RA) == 9 && render_swap_card(&RB) == 9 && SA.said.what == SWAP_SAID_SWAPPED && SB.said.what == SWAP_SAID_SWAPPED, "3a: no SWAPPED on both (A %d, B %d)", SA.said.what, SB.said.what);
    r_shot(&RA, "7_I_swapped");
    r_step((int)(SWAP_SAID_S / 0.033f) + 10);
    SCHECK(!render_swap_up(&RA) && !render_swap_up(&RB), "3a: the SWAPPED stamp stays up");
    if (fails == f0) printf("selftest-swap: 3a. GIVE -> SWAP, PICK -> a fish -> SWAP, THEY PICKED -> SWAP: traded in %.1f s, names, looks, a PARAGON's part and a lab fish's body kept, stages paid once, five and five\n", n * 0.033);
    r_down();

    /* 3b. NO THANKS next door: nothing changes */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a0 = RA.fish[0].uid;
      SCHECK(r_offer(0), "3b: no SWAP"); r_step(20);
      SCHECK(r_tap(&RB, RENDER_SW_BTN_RIGHT) == RENDER_SW_TAP_NO, "3b: NO THANKS did not answer");
      r_step(20);
      SCHECK(SA.out.st == SO_OFF && SA.said.what == SWAP_SAID_THANKS && render_swap_card(&RA) == 9, "3b: A did not hear NO THANKS (out %d, said %d)", SA.out.st, SA.said.what);
      SCHECK(r_slot(&RA, a0) >= 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP && nst(&SA) == SWN_NONE, "3b: something changed");
      r_shot(&RA, "8_I_no_thanks");
      SCHECK(r_tap(&RA, RENDER_SW_BTN_MID) == RENDER_SW_TAP_OK && !render_swap_up(&RA), "3b: OK did not close the card");
      if (fails == f1) printf("selftest-swap: 3b. NO THANKS next door: THEY SAID NO THANKS, OK, nothing changed\n");
      r_down(); }

    /* 3h. NO THANKS whose frame is lost: A's offer, sent again, is answered NO again - the card never comes back */
    { const int f1 = fails; r_setup(false); r_step(40);
      SCHECK(r_offer(0), "3h: no SWAP"); r_step(20);
      r_drop_swap_to[0] = true;                          /* B's NO never reaches A ... */
      SCHECK(r_tap(&RB, RENDER_SW_BTN_RIGHT) == RENDER_SW_TAP_NO, "3h: NO THANKS did not answer");
      r_step(90);                                        /* ... while A sends its OFFER again, every half second */
      SCHECK(SB.in.st == SI_OFF && !render_swap_up(&RB), "3h: the card came back after NO THANKS (in %d)", SB.in.st);
      r_drop_swap_to[0] = false; r_step(40);
      SCHECK(SA.out.st == SO_OFF && SA.said.what == SWAP_SAID_THANKS, "3h: A never heard NO THANKS (out %d, said %d)", SA.out.st, SA.said.what);
      if (fails == f1) printf("selftest-swap: 3h. NO THANKS lost on the way: the OFFER sent again is answered NO again, the card stays down\n");
      r_down(); }

    /* 3c. CALL OFF while asking, and B's own CALL OFF while waiting */
    { const int f1 = fails; r_setup(false); r_step(40);
      SCHECK(r_offer(0), "3c: no SWAP"); r_step(20);
      SCHECK(r_tap(&RA, RENDER_SW_BTN_MID) == RENDER_SW_TAP_CALLOFF, "3c: CALL OFF did not answer"); r_step(20);
      SCHECK(SA.out.st == SO_OFF && SB.in.st == SI_OFF && !render_swap_up(&RA), "3c: CALL OFF left a swap standing (A %d, B %d)", SA.out.st, SB.in.st);
      SCHECK(r_offer(1), "3c: no SWAP the second time"); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 0); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
      SCHECK(SB.in.st == SI_WAITING, "3c: B is not waiting (%d)", SB.in.st);
      SCHECK(r_tap(&RB, RENDER_SW_BTN_MID) == RENDER_SW_TAP_CALLOFF, "3c: B's CALL OFF did not answer"); r_step(20);
      SCHECK(SA.out.st == SO_OFF && SA.said.what == SWAP_SAID_CALLED_OFF && SB.in.st == SI_OFF, "3c: A did not hear THEY CALLED IT OFF (out %d, said %d)", SA.out.st, SA.said.what);
      SCHECK(RA.n_fish == POP_CAP && RB.n_fish == POP_CAP && nst(&SA) == SWN_NONE && nst(&SB) == SWN_NONE, "3c: something changed");
      if (fails == f1) printf("selftest-swap: 3c. CALL OFF on either side before the last yes: nothing changes, the other keeper is told\n");
      r_down(); }

    /* 3d. the link gone before the trade (B deaf as A's fish swims to the door): THE SWAP DID NOT HAPPEN */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a0 = RA.fish[0].uid;
      SCHECK(r_offer(0), "3d: no SWAP"); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 1); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
      r_deaf[0] = true;                                  /* A hears nothing from here on */
      r_tap(&RA, RENDER_SW_BTN_LEFT);
      r_step(400);
      SCHECK(SA.out.st == SO_OFF && nst(&SA) == SWN_NONE && SA.said.what == SWAP_SAID_NOT_HAPPENED, "3d: A (out %d, note %d, said %d)", SA.out.st, nst(&SA), SA.said.what);
      SCHECK(r_slot(&RA, a0) >= 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "3d: a fish moved with the link gone");
      r_shot(&RA, "9_I_not_happened");
      r_deaf[0] = false; r_step(1200);
      SCHECK(SB.in.st == SI_OFF && nst(&SB) == SWN_NONE && RB.n_fish == POP_CAP, "3d: B still holds the swap (in %d, note %d)", SB.in.st, nst(&SB));
      if (fails == f1) printf("selftest-swap: 3d. the link gone before the trade: THE SWAP DID NOT HAPPEN, both tanks as they were\n");
      r_down(); }

    /* 3e. a 0.4.2 tank next door: the GIVE card stays GIVE (THEIR TANK IS FULL) */
    { const int f1 = fails; r_setup(true); r_step(40);
      SCHECK(!SA.door_swap[LINK_RIGHT], "3e: a 0.4.2 tank reads as taking swaps");
      SCHECK(!r_offer(0) || SA.out.st == SO_OFF, "3e: SWAP offered to a 0.4.2 tank");
      render_give_modal_close();
      if (fails == f1) printf("selftest-swap: 3e. a 0.4.2 tank next door is never offered a swap\n");
      r_down(); }

    /* 3f. the hard case: B traded, every frame to A lost, and the two never link again - a day later A's fish comes home */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a0 = RA.fish[0].uid;
      SCHECK(r_offer(0), "3f: no SWAP"); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 3); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
      r_drop_swap_to[0] = true;                          /* B's DONE never reaches A */
      r_tap(&RA, RENDER_SW_BTN_LEFT);
      int k = 0; for (; k < 1500 && nst(&SB) != SWN_DONE; k++) r_step(1);
      SCHECK(nst(&SB) == SWN_DONE && nst(&SA) == SWN_COMMITTED && r_slot(&RB, a0) >= 0 && r_slot(&RA, a0) < 0, "3f: staging (B note %d, A note %d)", nst(&SB), nst(&SA));
      SCHECK(progression_fish_held(&RA) == POP_CAP && RA.n_fish == POP_CAP - 1, "3f: A's held place (%d fish, %d held)", RA.n_fish, progression_fish_held(&RA));
      link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
      n1(&SA)->wait_s = SWAP_WAIT_S - 60; n1(&SA)->part = 0;   /* (a day apart less a minute: no need to step it) */
      r_step(1500);
      SCHECK(nst(&SA) == SWN_COMMITTED, "3f: A let go before the day was over");
      r_double_ok = true; r_step(500);
      SCHECK(nst(&SA) == SWN_GAVEUP && r_slot(&RA, a0) >= 0 && SA.said.what == SWAP_SAID_NOT_FINISHED, "3f: after a day A's fish is not home (note %d, said %d)", nst(&SA), SA.said.what);
      r_shot(&RA, "10_I_not_finished");
      SCHECK(r_slot(&RB, a0) >= 0, "3f: B lost the fish it traded for");
      r_double_ok = false;
      if (fails == f1) printf("selftest-swap: 3f. the one hard case: a held place for a day, then THE SWAP DID NOT FINISH and A's fish home (a rare double, never a loss)\n");
      r_down(); }

    /* 3g. a crash at A after its note, before the trade answered: the note finishes it after the boot */
    { const int f1 = fails; r_setup(false); r_step(40);
      uint32_t a0 = RA.fish[0].uid, b0 = RB.fish[4].uid;
      SCHECK(r_offer(0), "3g: no SWAP"); r_step(20);
      r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 4); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
      r_drop_swap_to[0] = true;
      r_tap(&RA, RENDER_SW_BTN_LEFT);
      int k = 0; for (; k < 1500 && nst(&SB) != SWN_DONE; k++) r_step(1);
      swap_crash(&SA);                                   /* A's RAM gone: its note from the flash */
      tank_link_swap_boot(&RA);
      r_drop_swap_to[0] = false;
      for (k = 0; k < 600 && (nst(&SA) != SWN_NONE || nst(&SB) != SWN_NONE); k++) r_step(1);
      SCHECK(nst(&SA) == SWN_NONE && nst(&SB) == SWN_NONE && r_slot(&RA, b0) >= 0 && r_slot(&RB, a0) >= 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP,
             "3g: not settled after A's boot (A note %d, B note %d, b in A %d, a in B %d)", nst(&SA), nst(&SB), r_slot(&RA, b0), r_slot(&RB, a0));
      if (fails == f1) printf("selftest-swap: 3g. A booted mid-trade: its note finished the swap at the next frame from B (%.1f s)\n", k * 0.033);
      r_down(); }
    tank_link_swap_hour = NULL;
}

/* ===================== audit r1 (0.4.3): the cards and books around a swap (from verify/V2, its outcomes asserted) ===================== */
#include "notice.h"
static int nv_cards, nv_kinds[12]; static bool nv_prev;
static void nv_frame(const tank_t *t) {                  /* one notice frame: a card that comes up is counted once, then dismissed */
    notice_tick(t, 0.033f, false);
    const notice_t *c = notice_current();
    if (c && !nv_prev) { nv_cards++; if (c->kind >= 0 && c->kind < 12) nv_kinds[c->kind]++;
        if (getenv("NV_TRACE")) printf("  card kind %d fish %d (%s) bit %x n_fish %d\n", c->kind, c->fish, c->fish >= 0 && c->fish < t->n_fish ? t->fish[c->fish].name : "-", (unsigned)c->bit, t->n_fish); }
    if (c && c->since > 0) notice_dismiss();
    nv_prev = notice_current() != NULL;
}
static void nv_reset(void) { nv_cards = 0; memset(nv_kinds, 0, sizeof nv_kinds); nv_prev = false; }
static void nv_diversify(void) {                         /* R's fish all differ (badges, stages); A's offered fish an elder whose three are rolled */
    const uint32_t J = MS_REACHED_JUV | MS_REACHED_ADULT;
    RB.fish[0].ms_bits = J;                                                         RB.fish[0].stage = STAGE_ADULT;
    RB.fish[1].ms_bits = J | MS_REACHED_ELDER | MS_FIRST_DART | MS_FIRST_BUBBLES;   RB.fish[1].stage = STAGE_ELDER;
    RB.fish[2].ms_bits = J | MS_FIRST_FOLLOW | MS_FIRST_GRASS;                      RB.fish[2].stage = STAGE_ADULT;
    RB.fish[3].ms_bits = J | MS_FIRST_MEAL_FROM_YOU | MS_FIRST_HOLD_APPROACH;       RB.fish[3].stage = STAGE_ADULT;
    RB.fish[4].ms_bits = J;                                                         RB.fish[4].stage = STAGE_ADULT;
    RA.fish[0].stage = STAGE_ELDER; RA.fish[0].ms_bits = J | MS_REACHED_ELDER | MS_FIRST_MEAL_FROM_YOU | MS_FIRST_DART;
    RA.fish[0].evo = 0; RA.fish[0].evo_offer = 0; tank_evo_roll(&RA, &RA.fish[0]);
    RA.tank_ms_bits |= TMS_PAIR | TMS_TRIO | TMS_QUARTET | TMS_QUINTET; RB.tank_ms_bits |= TMS_PAIR | TMS_TRIO | TMS_QUARTET | TMS_QUINTET;
}
static int nv_slp1(int who) {                            /* SLP-1: cards on tank `who` (0 A offers, 1 B picks) through a swap of B's fish 0 */
    r_setup(false); r_step(40); nv_diversify();
    tank_t *W = who ? &RB : &RA;
    uint32_t a = RA.fish[0].uid, b = RB.fish[0].uid;
    notice_sync(W); nv_reset();
    SCHECK(r_offer(0), "SLP-1: no SWAP"); r_step(20);
    r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 0); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
    r_tap(&RA, RENDER_SW_BTN_LEFT);
    for (int f = 0; f < 1200; f++) { r_step(1); nv_frame(W); }
    SCHECK(r_slot(&RB, a) >= 0 && r_slot(&RA, b) >= 0, "SLP-1: no trade");
    int n = nv_cards; r_down(); return n;
}
static void part_notices(void) {
    const int f0 = fails;
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swapnotices.sav", 1);
    tank_link_swap_hour = r_hour; r_hour_now = 500000;
    {   int r = nv_slp1(1), i = nv_slp1(0);
        SCHECK(r == 0 && i == 0, "SLP-1: cards nobody earned after a swap: R (picks) %d, I (offers) %d (want 0, 0; 11 before the fix)", r, i);
        if (r == 0 && i == 0) printf("selftest-swap: 4. SLP-1: a swap brings no card on either tank (R had 11 before the audit's fix)\n"); }
    {   /* LK-8: A's side of a swap (its out, B never hears COMMIT) with a fry staged: the fry stays, its wait does not start over */
        r_setup(false); r_step(40);
        tank_remove_fish(&RA, 4); r_step(5);
        RA.player_feedings = 100; progression_stage_arrival(&RA);
        const bool pend0 = progression_arrival_pending(); const int32_t need0 = progression_dbg_sale_need();
        SCHECK(r_offer(0), "LK-8: no SWAP (A %d fish)", RA.n_fish); r_step(20);
        r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + 1); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
        r_drop_swap_to[1] = true; r_tap(&RA, RENDER_SW_BTN_LEFT);
        for (int k = 0; k < 600 && nst(&SA) != SWN_COMMITTED; k++) r_step(1);
        r_step(5);
        SCHECK(nst(&SA) == SWN_COMMITTED && pend0 && progression_arrival_pending() && progression_dbg_sale_need() == need0,
               "LK-8: a swap's departure ran a sale's books (fry staged %d -> %d, wait %d -> %d)", pend0, progression_arrival_pending(), need0, progression_dbg_sale_need());
        r_drop_swap_to[1] = false; r_down();
        if (fails == f0) printf("selftest-swap: 4. LK-8: a swap's departure keeps a staged fry and its wait (a sale's books are a sale's)\n"); }
    {   /* SLP-6: a sale in the frame another fish earns a badge - its card still comes */
        const int f1 = fails;
        r_setup(false); r_step(10); r_down();
        notice_sync(&RA); nv_reset(); nv_frame(&RA);
        RA.fish[2].ms_bits |= MS_FIRST_DART;
        bool sold = progression_sell_fish(&RA, 0);
        for (int f = 0; f < 600; f++) nv_frame(&RA);
        SCHECK(sold && nv_kinds[NOTICE_MILESTONE] == 1, "SLP-6: a sale the same frame dropped another fish's card (sold %d, MILESTONE %d)", sold, nv_kinds[NOTICE_MILESTONE]);
        if (fails == f1) printf("selftest-swap: 4. SLP-6: a sale the same frame keeps the card another fish earned\n"); }
    {   /* SPS-4: READY TO EVOLVE queued behind a page, the fish evolves on the page: that card never shows */
        const int f1 = fails;
        r_setup(false); r_step(10); r_down();
        RA.fish[0].stage = STAGE_ELDER; RA.fish[0].evo = 0; RA.fish[0].evo_offer = 0;
        notice_sync(&RA); nv_reset();
        tank_evo_roll(&RA, &RA.fish[0]);
        for (int f = 0; f < 30; f++) notice_tick(&RA, 0.033f, true);   /* (a page up: queued) */
        SCHECK(notice_pending() >= 1, "SPS-4: staging - READY TO EVOLVE not queued");
        SCHECK(progression_evo_keep(&RA, 0, 1), "SPS-4: keep");
        nv_reset();
        for (int f = 0; f < 600; f++) nv_frame(&RA);
        SCHECK(nv_kinds[NOTICE_EVOLVE] == 0 && nv_kinds[NOTICE_PARAGON] == 1, "SPS-4: after evolving, cards EVOLVE %d PARAGON %d (want 0, 1)", nv_kinds[NOTICE_EVOLVE], nv_kinds[NOTICE_PARAGON]);
        if (fails == f1) printf("selftest-swap: 4. SPS-4: a fish that evolved under a page never shows its READY TO EVOLVE card after\n"); }
    tank_link_swap_hour = NULL;
}
/* ===================== audit r1 (0.4.3): the notes' cases on two real tanks (from verify/V3, outcomes asserted) ===================== */
/* the hard case: A offers fish 0, B picks ib, every swap frame to A lost from A's last SWAP; B applies (DONE), A COMMITTED; the link closes */
static bool v_stage_hard(int ib, uint32_t *a0, uint32_t *b0) {
    r_setup(false); r_step(40);
    *a0 = RA.fish[0].uid; *b0 = RB.fish[ib].uid;
    if (!r_offer(0)) return false;
    r_step(20);
    r_tap(&RB, RENDER_SW_BTN_LEFT); r_tap(&RB, RENDER_SW_BTN_TILE0 + ib); r_tap(&RB, RENDER_SW_BTN_LEFT); r_step(20);
    r_drop_swap_to[0] = true;
    r_tap(&RA, RENDER_SW_BTN_LEFT);
    for (int k = 0; k < 1500 && !(nst(&SB) == SWN_DONE && n1(&SB)->applied); k++) r_step(1);
    link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
    r_drop_swap_to[0] = false;
    return nst(&SB) == SWN_DONE && n1(&SB)->applied && nst(&SA) == SWN_COMMITTED && n1(&SA)->applied;
}
static void v_relink(int frames) { link_mode(LA, true); link_mode(LB, true); r_step(10); link_arm(LA, LINK_RIGHT); r_step(5); link_arm(LB, LINK_LEFT); r_step(frames); }
static int v_where(uint32_t u) { return (r_slot(&RA, u) >= 0 ? 1 : 0) | (r_slot(&RB, u) >= 0 ? 2 : 0); }
static void part_v3(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swapv3.sav", 1);
    tank_link_swap_hour = r_hour; r_hour_now = 500000;
    uint32_t a0, b0;
    {   /* LK-3: B finishes many swaps meanwhile and is unlinked a long time - its note waits; at the next link a's COMMIT is DONE, never ABORT */
        const int f1 = fails;
        SCHECK(v_stage_hard(3, &a0, &b0), "LK-3: staging");
        for (int k = 0; k < 3000; k++) r_step(1);                       /* (100 s apart: R lets nothing go) */
        SCHECK(nst(&SB) == SWN_DONE && !swap_busy(&SB), "LK-3: R's note let go or R busy (note %d, busy %d)", nst(&SB), swap_busy(&SB));
        v_relink(300);
        if (getenv("V3_TRACE")) printf("  LK-3 relink: A right %08x B left %08x | A note st %d applied %d sent %u | B note st %d\n", (unsigned)link_neighbor(LA, LINK_RIGHT), (unsigned)link_neighbor(LB, LINK_LEFT),
                                       nst(&SA), n1(&SA) ? n1(&SA)->applied : -1, n1(&SA) ? n1(&SA)->sent : 0, nst(&SB));
        SCHECK(v_where(a0) == 2 && v_where(b0) == 1 && nnotes(&SA) == 0 && nnotes(&SB) == 0, "LK-3: after the link a in %d, b in %d (want B=2, A=1), notes %d/%d",
               v_where(a0), v_where(b0), nnotes(&SA), nnotes(&SB));
        r_down();
        if (fails == f1) printf("selftest-swap: 5. LK-3: R's note waits for CLOSED however long apart - the late COMMIT gets DONE, the trade finishes\n"); }
    {   /* PW-6: the hard case 23 h 59 apart (awake), then both off a day and A boots beside B: its first tick brings nothing home, the link finishes it */
        const int f1 = fails;
        SCHECK(v_stage_hard(4, &a0, &b0), "PW-6: staging");
        n1(&SA)->wait_s = SWAP_WAIT_S - 60; swap_save(&SA);
        swap_crash(&SA); swap_crash(&SB); tank_link_swap_boot(&RA); tank_link_swap_boot(&RB);
        rnow += 25u * 3600u * 1000u;                                     /* (a day off: the board's ms jumps) */
        tank_link_tick(&RA, NULL); r_step(3);
        SCHECK(nst(&SA) == SWN_COMMITTED && v_where(a0) == 2, "PW-6: the boot's first tick ended the note (note %d, a in %d)", nst(&SA), v_where(a0));
        v_relink(300);
        SCHECK(v_where(a0) == 2 && v_where(b0) == 1 && nnotes(&SA) == 0 && nnotes(&SB) == 0, "PW-6: after the link a in %d, b in %d", v_where(a0), v_where(b0));
        r_down();
        if (fails == f1) printf("selftest-swap: 5. PW-6: a day off counts nothing; the boot beside B lets the link finish it - no double\n"); }
    {   /* LK-4 (I, no clock) + LK-10: a day of awake minutes apart brings a home (the hard case, by design); R then is not BUSY, and at
           the next link R's DONE gets CLOSED (I has no note), R's note goes, and a new swap is taken */
        const int f1 = fails;
        SCHECK(v_stage_hard(2, &a0, &b0), "LK-4: staging");
        n1(&SA)->wait_s = SWAP_WAIT_S - 60; n1(&SA)->part = 0;
        r_double_ok = true;
        r_step(2200);                                    /* (past the minute) */
        SCHECK(nst(&SA) == SWN_GAVEUP && SA.said.what == SWAP_SAID_NOT_FINISHED && (v_where(a0) & 1), "LK-4: a day apart, a not home (note %d, said %d)", nst(&SA), SA.said.what);
        SCHECK(!swap_busy(&SB), "LK-10: R busy while it waits");
        v_relink(600);
        SCHECK(nnotes(&SB) == 0 && nnotes(&SA) == 0 && v_where(a0) == 2 && v_where(b0) == 1, "LK-10 / Q1: after the link the trade did not finish (notes %d/%d, a in %d, b in %d)",
               nnotes(&SA), nnotes(&SB), v_where(a0), v_where(b0));
        render_swap_forget();
        SCHECK(r_offer(1), "LK-10: no SWAP offered"); r_step(40);
        SCHECK(SB.in.st == SI_ASKED, "LK-10: B did not take a new swap (in %d, A said %d)", SB.in.st, SA.said.what);
        r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 5. LK-4/LK-10: no clock, a day apart: a home (the hard case); R never BUSY; at the next link the trade finishes (Q1)\n"); }
    {   /* review 11-22: MY TANKS on R's board mid-hard-case - the shelved tank's note never answers under the new tank's name; that
           tank back on the glass, the trade finishes. Never a double, never a loss */
        const int f1 = fails;
        SCHECK(v_stage_hard(1, &a0, &b0), "MY TANKS: staging");
        const uint32_t B1 = RB.uid, B2 = RB.uid ^ 0x5A5A0000u;
        RB.uid = B2; swap_crash(&SB); tank_link_swap_boot(&RB);
        link_host_t hb = tank_link_host(&RB, r_send); link_init(LB, RB.uid, &hb, 3, rnow);
        v_relink(600);
        SCHECK(link_neighbor(LA, LINK_RIGHT) == B2, "MY TANKS: A never linked with B2");
        SCHECK(nnotes(&SB) == 1 && n1(&SB)->tank == B1 && nst(&SA) == SWN_COMMITTED && v_where(a0) == 2,
               "MY TANKS: B1's note let go under B2's name (B notes %d, A note %d, a in %d)", nnotes(&SB), nst(&SA), v_where(a0));
        RB.uid = B1; swap_crash(&SB); tank_link_swap_boot(&RB);
        link_close(LA, LINK_RIGHT); r_step(30);
        hb = tank_link_host(&RB, r_send); link_init(LB, RB.uid, &hb, 4, rnow);
        v_relink(600);
        SCHECK(v_where(a0) == 2 && v_where(b0) == 1 && nnotes(&SA) == 0 && nnotes(&SB) == 0, "MY TANKS: B1 back, not settled (a in %d, b in %d, notes %d/%d)",
               v_where(a0), v_where(b0), nnotes(&SA), nnotes(&SB));
        r_down();
        if (fails == f1) printf("selftest-swap: 5. MY TANKS on R's board mid-trade: the shelved tank's note waits silent under another tank, finishes when it is back\n"); }
    {   /* LK-7: the breeding parent swapped - its pair ends, the 50 back; the bowl holds a fish in a swap */
        const int f1 = fails;
        r_setup(false); r_step(40);
        uint32_t a = RA.fish[0].uid, b = RB.fish[2].uid;
        RA.sd_unlocks |= SD_ITEM_BREED; RA.breed_a = a; RA.breed_b = RA.fish[1].uid; RA.breed_paid = 50; const int bal0 = RA.sd_balance;
        SCHECK(r_through(0, 2), "LK-7: no last yes");
        SCHECK(progression_move_why(&RA, r_slot(&RA, a)) != BOWL_OK, "LK-7: the bowl takes a fish in a swap");
        r_tap(&RA, RENDER_SW_BTN_LEFT);
        SCHECK(r_settle(a, b) >= 0, "LK-7: no trade");
        progression_breed_check(&RA);
        SCHECK(!RA.breed_a && !RA.breed_b && RA.breed_paid == 0 && RA.sd_balance == bal0 + 50, "LK-7: the pair (%08x/%08x paid %d) not undone, balance %d (was %d)",
               (unsigned)RA.breed_a, (unsigned)RA.breed_b, RA.breed_paid, RA.sd_balance, bal0);
        r_down();
        if (fails == f1) printf("selftest-swap: 5. LK-7: a breeding parent swapped - the pair ends, its 50 back; the bowl takes no fish in a swap\n"); }
    {   /* PW-7: an open note ticks without a save each tick (the old u16 hour 0 saved every tick); its wait saves every 10 minutes */
        memset(M, 0, sizeof M); m_crashes = false;
        swap_book_t *fb = &M[0].B; swap_host_t h = m_host(0);
        fb->notes[0].st = SWN_COMMITTED; fb->notes[0].role = SWAP_ROLE_I; fb->notes[0].applied = 1; fb->notes[0].sid = 5; fb->notes[0].peer = 0xB1u; fb->notes[0].tank = 0xA1u;
        fb->notes[0].out = mk_pp(0x991u, 0xA1u, "away"); fb->notes[0].in = mk_pp(0x992u, 0xB1u, "coming");
        int saves = 0; uint32_t t = 5000;
        for (int k = 0; k < 20000; k++) { fb->mem_len = 0; t += 33; swap_tick(fb, NULL, &h, 0xA1u, t, 1.0 + k * 0.033); if (fb->mem_len) saves++; }   /* (10 minutes) */
        SCHECK(saves == 1, "PW-7: %d saves in 11 minutes (want 1)", saves);
        if (saves == 1) printf("selftest-swap: 5. PW-7: no clock in the wait - no per-tick saves (one in 10 minutes), no rebase or wrap to get wrong\n"); }
    tank_link_swap_hour = NULL;
}
/* ===================== audit r1 (0.4.3), LK-C: THE CHAOS on two real tanks ===================== */
/* tank_link's host and the real link engine, a hostile radio (lost, copied, reordered), keepers at random, the book crashing
   at random on either side, its saves refused at random, the doors closed and made again, fish visiting. Every step: never
   home in both tanks (r_step), never alive nowhere without a note. Healed: every note settled, 5 and 5, every fish once */
static void c_keeper(tank_t *T, swap_book_t *b, uint32_t other) {
    if (!swap_busy(b) && T->n_fish && (rnd() % 300) == 0) { b->want_uid = T->fish[rnd() % (uint32_t)T->n_fish].uid; b->want_peer = other; }
    if (b->out.st == SO_ASKING && (rnd() % 3000) == 0) b->i_cancel = true;
    if (b->in.st == SI_ASKED && (rnd() % 60) == 0) { if (rndf() < 0.15f) b->r_answer = -1; else b->r_pick = T->fish[rnd() % (uint32_t)T->n_fish].uid; }
    if (b->in.st == SI_WAITING && (rnd() % 4000) == 0) b->r_cancel = true;
    if (b->out.st == SO_PICKED && (rnd() % 60) == 0) b->i_answer = rndf() < 0.85f ? 1 : -1;
}
static bool c_note_holds(uint32_t uid) {
    const swap_book_t *B2[2] = { &SA, &SB };
    for (int k = 0; k < 2; k++) for (int j = 0; j < SWAP_NOTES; j++) { const swap_note_t *n = &B2[k]->notes[j]; if (n->st != SWN_NONE && (n->out.uid == uid || n->in.uid == uid)) return true; }
    return false;
}
static int c_swapped_total;
static void lk_chaos(uint32_t seed, int minutes, bool quiet) {
    const int f1 = fails;
    s_rng = seed * 2654435761u + 11;
    r_setup(false); r_step(40);
    uint32_t U2[2 * POP_CAP]; for (int i = 0; i < POP_CAP; i++) { U2[i] = RA.fish[i].uid; U2[POP_CAP + i] = RB.fish[i].uid; }   /* (every fish of both full tanks: 10 on a pocket tank's pair, 20 on the big screens') */
    c_chaos = true; cqn = 0;
    int crashes = 0, swapped = 0, relinks = 0, visits = 0, refusals = 0;
    tank_t *T2[2] = { &RA, &RB }; swap_book_t *B2[2] = { &SA, &SB };
    const int steps = minutes * 60 * 30;
    for (int st = 0; st < steps; st++) {
        for (int k = 0; k < 2; k++) {
            c_keeper(T2[k], B2[k], T2[1 - k]->uid);
            if ((rnd() % 2500) == 0) { swap_crash(B2[k]); tank_link_swap_boot(T2[k]); crashes++; }   /* the book's RAM gone, its boot */
            B2[k]->fail_save = (rnd() % 200) == 0; if (B2[k]->fail_save) refusals++;
            if ((rnd() % 1500) == 0 && T2[k]->n_fish) {      /* a fish of ours goes visiting */
                fish_t *f = &T2[k]->fish[rnd() % (uint32_t)T2[k]->n_fish];
                if (!f->away && f->door == DOOR_NONE) { f->goal.id = GOAL_EXPLORE; f->door = DOOR_HEADING; f->door_edge = (int8_t)(k ? LINK_LEFT : LINK_RIGHT); f->door_refused = 0; visits++; }
            }
        }
        if ((rnd() % 9000) == 0) { link_close(LA, LINK_RIGHT); relinks++; }
        if (!link_neighbor(LA, LINK_RIGHT) && (st % 90) == 0) { link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); link_arm(LB, LINK_LEFT); }
        if ((rnd() % 4000) == 0) { r_deaf[rnd() & 1] = true; } else if ((rnd() % 300) == 0) r_deaf[0] = r_deaf[1] = false;
        uint8_t was0 = SA.said.what;
        r_step(1);
        if (SA.said.what == SWAP_SAID_SWAPPED && was0 != SWAP_SAID_SWAPPED) swapped++;
        for (int u = 0; u < 2 * POP_CAP; u++) {
            int n = (r_slot(&RA, U2[u]) >= 0) + (r_slot(&RB, U2[u]) >= 0);
            SCHECK(n == 1 || c_note_holds(U2[u]), "LK-C seed %u t=%.1f: fish %08x is in %d tanks and no note holds it", seed, rnow / 1000.0, (unsigned)U2[u], n);
            SCHECK(n <= 1, "LK-C seed %u t=%.1f: fish %08x is home in both tanks", seed, rnow / 1000.0, (unsigned)U2[u]);
        }
        if (fails - f1 > 10) break;
    }
    /* heal */
    c_chaos = false; cqn = 0; r_deaf[0] = r_deaf[1] = false; SA.fail_save = SB.fail_save = false;
    for (int st = 0; st < 30 * 120 && (nnotes(&SA) || nnotes(&SB) || SA.out.st || SA.in.st || SB.out.st || SB.in.st || !link_neighbor(LA, LINK_RIGHT)); st++) {
        if (!link_neighbor(LA, LINK_RIGHT) && (st % 90) == 0) { link_mode(LA, true); link_mode(LB, true); link_arm(LA, LINK_RIGHT); link_arm(LB, LINK_LEFT); }
        for (int k = 0; k < 2; k++) { if (B2[k]->in.st == SI_ASKED) B2[k]->r_answer = -1; if (B2[k]->out.st == SO_PICKED) B2[k]->i_answer = -1; }
        r_step(1);
    }
    for (int k = 0; k < 2; k++) for (int i = 0; i < T2[k]->n_fish; i++) T2[k]->fish[i].door_seen = T2[k]->clock + 1e6;   /* (everyone home) */
    r_step(30 * 40);
    SCHECK(nnotes(&SA) == 0 && nnotes(&SB) == 0, "LK-C seed %u: notes still open after the heal (A %d, B %d)", seed, nnotes(&SA), nnotes(&SB));
    SCHECK(RA.n_fish == POP_CAP && RB.n_fish == POP_CAP, "LK-C seed %u: %d / %d fish at the end", seed, RA.n_fish, RB.n_fish);
    for (int u = 0; u < 2 * POP_CAP; u++) { int n = (r_slot(&RA, U2[u]) >= 0) + (r_slot(&RB, U2[u]) >= 0); SCHECK(n == 1, "LK-C seed %u: fish %08x in %d tanks at the end", seed, (unsigned)U2[u], n); }
    c_swapped_total += swapped;
    if (!quiet)
        printf("selftest-swap: 6. LK-C chaos seed %u, %d min, %.0f%% lost: %d swaps, %d book crashes, %d refused saves, %d visits sent, %d doors closed%s\n",
               seed, minutes, c_loss * 100, swapped, crashes, refusals, visits, relinks, fails != f1 ? " - FAILED" : "");
    r_down();
}
static void part_chaos(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swapchaos.sav", 1);
    tank_link_swap_hour = r_hour; r_hour_now = 500000;
    if (getenv("SWAP_CHAOS_LOSS")) c_loss = (float)atoi(getenv("SWAP_CHAOS_LOSS")) / 100.0f;
    int seeds = getenv("SWAP_CHAOS_SEEDS") ? atoi(getenv("SWAP_CHAOS_SEEDS")) : 2, mins = getenv("SWAP_CHAOS_MIN") ? atoi(getenv("SWAP_CHAOS_MIN")) : 2;
    c_swapped_total = 0;
    for (int sd = 1; sd <= seeds; sd++) lk_chaos((uint32_t)sd, mins, false);
    SCHECK(c_swapped_total > 0, "LK-C: no swap finished in the chaos (it proves nothing)");
    for (int pl = 1; pl <= 3 && !getenv("SWAP_CHAOS_NO_PLANT"); pl++) {   /* the chaos must see the planted bugs */
        const int f0 = fails; swap_test_plant = pl; s_mute = true;
        for (int sd = 1; sd <= 6 && fails == f0; sd++) lk_chaos(200 + (uint32_t)sd, POP_CAP > 5 ? 2 * mins : mins, true);   /* (the big screens: ten fish a tank, a swap no more often - the planted runs twice as long, 11-23) */
        swap_test_plant = 0; s_mute = false;
        const bool seen = fails > f0; fails = f0;
        if (pl == 2) SCHECK(seen, "LK-C: planted bug 2 (R's note gone at DONE) went unseen by the chaos");
        printf("selftest-swap: 6. LK-C planted bug %d: %s\n", pl, seen ? "seen, as it must be" : pl == 1 ? "not seen here (it needs a refused save at the commit itself: the book soak, part 2, sees it)"
                                                                     : pl == 3 ? "not seen here (it needs the visiting ABORT race: case 3o sees it)" : "NOT SEEN");
    }
    c_loss = 0.10f;
    tank_link_swap_hour = NULL;
}

/* ===================== audit r2 (0.4.3): R2-1 (an R note's own end), R2-2 (the day apart by the wall clock), R2-L28 ===================== */
/* a session of the mock book's tank: `secs` awake at `dt` (the tank clock and, if set, the wall clock run together), then off `off`
   seconds (park: the sleep's save; none = a power cut) and a boot. Returns true when the note of `uid` ended with it home */
static double q_clk; static double q_wall; static uint32_t q_ms;
static void q_tick(swap_book_t *fb, swap_host_t *h, uint32_t tank, double dt, bool clocked) {
    q_clk += dt; q_ms += (uint32_t)(dt * 1000.0); if (clocked) { q_wall += dt; m_wall_now[0] = (int64_t)q_wall; } else m_wall_now[0] = 0;
    swap_tick(fb, NULL, h, tank, q_ms, q_clk);
}
static void q_off(swap_book_t *fb, swap_host_t *h, uint32_t tank, double off, bool park, bool clocked) {
    if (park) swap_park(fb);
    q_wall += off; q_clk += off;                         /* (a boot lives the span) */
    m_wall_now[0] = clocked ? (int64_t)q_wall : 0;
    swap_boot(fb, h, tank);
}
static int q_day_home(double awake_min, int sessions, bool park, bool clocked, int days_max, uint32_t *left_s) {
    memset(M, 0, sizeof M); m_crashes = false; M[0].uid = 0xBEEFu;
    swap_book_t *fb = &M[0].B; swap_host_t h = m_hostw(0);
    q_clk = 100; q_wall = 1800000000.0; q_ms = 5000; m_wall_now[0] = clocked ? (int64_t)q_wall : 0;
    swap_note_t ix; memset(&ix, 0, sizeof ix);
    ix.st = SWN_COMMITTED; ix.role = SWAP_ROLE_I; ix.applied = 1; ix.sid = 5; ix.peer = 7; ix.tank = 0xBEEFu;
    ix.out = mk_pp(0x991u, 0xBEEFu, "away"); ix.in = mk_pp(0x992u, 7, "other");
    fb->notes[0] = ix; swap_save(fb); swap_boot(fb, &h, 0xBEEFu);   /* (the note as the trade's moment saved it: no stamp yet) */
    const double dt = awake_min >= 600 ? 1.0 : 0.25;
    for (int day = 1; day <= days_max; day++)
        for (int s2 = 0; s2 < sessions; s2++) {
            for (double x = 0; x < awake_min * 60.0; x += dt) { q_tick(fb, &h, 0xBEEFu, dt, clocked); if (m_find(&M[0], 0x991u) >= 0) return day; }
            q_off(fb, &h, 0xBEEFu, (1440.0 - sessions * awake_min) * 60.0 / sessions, park, clocked);
            if (m_find(&M[0], 0x991u) >= 0) return day;
        }
    if (left_s) *left_s = n1(fb) ? n1(fb)->wait_s : 0;
    return -1;
}
static void part_r2(void) {
    {   /* R2-2: a tank with a set clock - a day by the wall clock, sleep and power-off included, however short its sittings (V1 c2, PW G).
           No sleep save: a power cut after each sitting (the stamp counts from the trade's first tick) */
        const int f1 = fails;
        static const struct { const char *name; double m; int n; } P[] = { { "all day", 1440, 1 }, { "1 h", 60, 1 }, { "4 x 15 min", 15, 4 },
                                                                           { "3 x 20 min", 20, 3 }, { "6 x 9 min", 9, 6 }, { "12 x 5 min", 5, 12 }, { "1 x 9.5 min", 9.5, 1 } };
        for (unsigned p = 0; p < sizeof P / sizeof P[0]; p++) {
            uint32_t left = 0; const int d = q_day_home(P[p].m, P[p].n, false, true, 6, &left);
            SCHECK(d >= 1 && d <= 2, "R2-2: clock set, %s a day, power cuts: a home on day %d (want 1-2; wait %u s)", P[p].name, d, left);
        }
        if (fails == f1) printf("selftest-swap: 7. R2-2: a set clock - a home within a day apart by the wall clock (sleep and power-off count), 5 min a day to all day\n"); }
    {   /* R2-2, no clock: awake time only (LK-4) - but kept at every sleep (swap_park), so short sittings add up */
        const int f1 = fails;
        uint32_t left = 0;
        int d = q_day_home(9, 6, true, false, 40, &left);
        SCHECK(d >= 26 && d <= 28, "R2-2: no clock, 6 x 9 min a day, sleeps: a home on day %d (want 27: 54 min a day; wait %u s)", d, left);
        d = q_day_home(60, 1, true, false, 40, &left);
        SCHECK(d >= 24 && d <= 25, "R2-2: no clock, 1 h a day: a home on day %d (want 24-25)", d);
        if (fails == f1) printf("selftest-swap: 7. R2-2: no clock - a day of awake time, kept at each sleep (6 x 9 min a day: day 27; was never)\n"); }
    {   /* R2-2's count, exact: 12 h awake at 40 ms frames counts 12 h (it ran 1.1-2.5 %% short) */
        memset(M, 0, sizeof M); swap_book_t *fb = &M[0].B; swap_host_t h = m_hostw(0);
        swap_note_t ix; memset(&ix, 0, sizeof ix);
        ix.st = SWN_COMMITTED; ix.role = SWAP_ROLE_I; ix.applied = 1; ix.sid = 5; ix.peer = 7; ix.tank = 0xBEEFu;
        ix.out = mk_pp(0x991u, 0xBEEFu, "away"); ix.in = mk_pp(0x992u, 7, "other");
        fb->notes[0] = ix; q_clk = 1; q_ms = 0; q_wall = 0; m_wall_now[0] = 0;
        for (long k = 0; k < 12L * 3600 * 25; k++) q_tick(fb, &h, 0xBEEFu, 0.04, false);
        const uint32_t w = n1(fb) ? n1(fb)->wait_s : 0;
        SCHECK(w >= 43190 && w <= 43210, "R2-2: 12 h awake counted %u s (want 43200)", w);
        if (w >= 43190 && w <= 43210) printf("selftest-swap: 7. R2-2: 12 h awake at 40 ms frames counts %u s (was 702 minutes)\n", w); }
    {   /* PW-6 with the wall clock: 23 h 59 apart, 25 h off, the boot: no end in its first minute (a boot beside the partner links first);
           with no partner there it ends just past the minute */
        const int f1 = fails;
        memset(M, 0, sizeof M); M[0].uid = 0xBEEFu; swap_book_t *fb = &M[0].B; swap_host_t h = m_hostw(0);
        swap_note_t ix; memset(&ix, 0, sizeof ix);
        ix.st = SWN_COMMITTED; ix.role = SWAP_ROLE_I; ix.applied = 1; ix.sid = 5; ix.peer = 7; ix.tank = 0xBEEFu;
        ix.out = mk_pp(0x991u, 0xBEEFu, "away"); ix.in = mk_pp(0x992u, 7, "other");
        q_wall = 1800000000.0; q_clk = 10; q_ms = 0;
        ix.wait_s = SWAP_WAIT_S - 60; ix.wall_at = (uint32_t)q_wall;
        fb->notes[0] = ix; swap_save(fb);
        q_off(fb, &h, 0xBEEFu, 25 * 3600.0, false, true);
        for (int k = 0; k < 58 * 4; k++) q_tick(fb, &h, 0xBEEFu, 0.25, true);
        SCHECK(nst(fb) == SWN_COMMITTED && m_find(&M[0], 0x991u) < 0, "PW-6: the boot's first minute ended the note (note %d)", nst(fb));
        for (int k = 0; k < 4 * 4; k++) q_tick(fb, &h, 0xBEEFu, 0.25, true);
        SCHECK(nst(fb) == SWN_GAVEUP && m_find(&M[0], 0x991u) >= 0, "PW-6: past the boot's minute, no partner: a not home (note %d)", nst(fb));
        if (fails == f1) printf("selftest-swap: 7. PW-6 (wall clock): 25 h off counts, but nothing ends in a boot's first minute - the link comes first\n"); }
    {   /* PW-7 with the wall clock: awake, a clock set 3 days ahead, a bad read, a clock set back - none counts; the count runs on */
        const int f1 = fails;
        memset(M, 0, sizeof M); M[0].uid = 0xBEEFu; swap_book_t *fb = &M[0].B; swap_host_t h = m_hostw(0);
        swap_note_t ix; memset(&ix, 0, sizeof ix);
        ix.st = SWN_COMMITTED; ix.role = SWAP_ROLE_I; ix.applied = 1; ix.sid = 5; ix.peer = 7; ix.tank = 0xBEEFu;
        ix.out = mk_pp(0x991u, 0xBEEFu, "away"); ix.in = mk_pp(0x992u, 7, "other");
        fb->notes[0] = ix; q_wall = 1800000000.0; q_clk = 10; q_ms = 0; swap_save(fb); swap_boot(fb, &h, 0xBEEFu);
        for (int k = 0; k < 1800; k++) q_tick(fb, &h, 0xBEEFu, 1.0, true);
        q_wall += 3 * 86400.0;                                       /* the keeper sets the clock 3 days on */
        for (int k = 0; k < 1800; k++) q_tick(fb, &h, 0xBEEFu, 1.0, true);
        for (int k = 0; k < 1800; k++) q_tick(fb, &h, 0xBEEFu, 1.0, false);   /* a bad read: no clock half an hour */
        q_wall -= 5 * 86400.0;                                       /* set back */
        for (int k = 0; k < 1800; k++) q_tick(fb, &h, 0xBEEFu, 1.0, true);
        const uint32_t w = n1(fb) ? n1(fb)->wait_s : 0;
        SCHECK(nst(fb) == SWN_COMMITTED && w >= 7190 && w <= 7210, "PW-7: a clock set or a bad read counted (note %d, wait %u s, want 7200)", nst(fb), w);
        q_off(fb, &h, 0xBEEFu, 60, false, true);                     /* a boot a minute later: the span since the last stamp only */
        const uint32_t w2 = n1(fb) ? n1(fb)->wait_s : 0;
        q_tick(fb, &h, 0xBEEFu, 0.25, true);
        SCHECK(n1(fb) && n1(fb)->wait_s >= w2 && n1(fb)->wait_s <= 7200 + 600 + 70, "PW-7: the boot after counted %u s (the last save's stamp: within ten minutes and the minute off)", n1(fb) ? n1(fb)->wait_s : 0);
        if (fails == f1) printf("selftest-swap: 7. PW-7 (wall clock): awake, a clock set on or back or a bad read counts nothing - the tank's own seconds do\n"); }
    {   /* R2-1: four applied R notes, the partner never back (a RESET there, CLOSED lost): a week apart they make room (not BUSY), a month
           apart they go. Daily 30-minute sittings, the rest asleep. No clock: the same by awake time */
        const int f1 = fails;
        for (int clocked = 1; clocked >= 0; clocked--) {
            memset(M, 0, sizeof M); M[0].uid = 0xBEEFu; swap_book_t *fb = &M[0].B; swap_host_t h = m_hostw(0);
            for (int k = 0; k < SWAP_NOTES; k++) {
                swap_note_t rn; memset(&rn, 0, sizeof rn);
                rn.st = SWN_DONE; rn.role = SWAP_ROLE_R; rn.applied = 1; rn.sid = 60 + (uint32_t)k; rn.peer = 0x7A7Au; rn.tank = 0xBEEFu;
                rn.out = mk_pp(0x700u + (uint32_t)k, 0xBEEFu, "b"); rn.in = mk_pp(0x800u + (uint32_t)k, 0x7A7Au, "a");
                fb->notes[k] = rn;
            }
            q_wall = 1800000000.0; q_clk = 10; q_ms = 0; m_wall_now[0] = clocked ? (int64_t)q_wall : 0;
            swap_save(fb); swap_boot(fb, &h, 0xBEEFu);
            SCHECK(swap_busy(fb), "R2-1: four notes and room (%d)", nnotes(fb));
            int day_room = -1, day_gone = -1;
            for (int day = 1; day <= 33 && day_gone < 0; day++) {
                for (int k = 0; k < (clocked ? 1800 : 24 * 3600); k += 1) q_tick(fb, &h, 0xBEEFu, 1.0, clocked);   /* (no clock: awake all day) */
                if (day_room < 0 && !swap_busy(fb)) { day_room = day; SCHECK(nnotes(fb) == SWAP_NOTES, "R2-1: a note let go before its week (%d)", nnotes(fb)); }
                if (nnotes(fb) == 0) day_gone = day;
                if (clocked) q_off(fb, &h, 0xBEEFu, 86400 - 1800, true, true);
                else { swap_park(fb); swap_boot(fb, &h, 0xBEEFu); }
            }
            SCHECK(day_room >= 7 && day_room <= 8 && day_gone >= 30 && day_gone <= 31, "R2-1 (%s): room on day %d (want 7-8), the notes gone on day %d (want 30-31)",
                   clocked ? "clock" : "no clock", day_room, day_gone);
        }
        if (fails == f1) printf("selftest-swap: 7. R2-1: an R note whose partner never comes back gives its room a week apart and goes a month apart (clock or none)\n"); }

    {   /* review r2: the FIRST time sync moves the clock (nothing lived): the notes' stamps move with it - a board 20 days behind that
           syncs at a boot counts only the hour it was really off */
        const int f1 = fails;
        memset(M, 0, sizeof M); M[0].uid = 0xBEEFu; swap_book_t *fb = &M[0].B; swap_host_t h = m_hostw(0);
        swap_attach((void *)0x2u, fb);
        swap_note_t ix; memset(&ix, 0, sizeof ix);
        ix.st = SWN_COMMITTED; ix.role = SWAP_ROLE_I; ix.applied = 1; ix.sid = 5; ix.peer = 7; ix.tank = 0xBEEFu;
        ix.out = mk_pp(0x991u, 0xBEEFu, "away"); ix.in = mk_pp(0x992u, 7, "other");
        q_wall = 1800000000.0; ix.wait_s = 3600; ix.wall_at = (uint32_t)q_wall; fb->notes[0] = ix; swap_save(fb);
        const int64_t late = (int64_t)q_wall + 3600, real = late + 20 * 86400;   /* (an hour off on the late clock; the internet says 20 days on) */
        swap_rebase(real - late, real);                      /* (before the clock moves: progression_clock_rebase) */
        q_wall = (double)real; q_clk = 10; q_ms = 0; m_wall_now[0] = real; swap_boot(fb, &h, 0xBEEFu);
        q_tick(fb, &h, 0xBEEFu, 0.25, true);
        SCHECK(n1(fb) && n1(fb)->wait_s >= 3600 + 3590 && n1(fb)->wait_s <= 3600 + 3610, "rebase: the first sync counted %u s apart (want 7200: the jump lives nothing)", n1(fb) ? n1(fb)->wait_s : 0);
        swap_rebase(real - late, real); swap_crash(fb);      /* (a cut: done again - the stamp never passes now, so it counts less, never more) */
        SCHECK(n1(fb) && n1(fb)->wall_at <= (uint32_t)real, "rebase: done twice, the stamp passed now");
        swap_attach((void *)0x2u, NULL);
        if (fails == f1) printf("selftest-swap: 7. the first time sync: the notes' stamps move with the clock - the jump counts nothing (review r2)\n"); }
    {   /* R2-L28: at the boot, a note of a tank this board no longer holds goes; a shelved tank's stays */
        memset(M, 0, sizeof M); swap_book_t *fb = &M[0].B; swap_host_t h = m_hostw(0);
        swap_note_t ix; memset(&ix, 0, sizeof ix);
        ix.st = SWN_COMMITTED; ix.role = SWAP_ROLE_I; ix.applied = 1; ix.sid = 5; ix.peer = 7;
        ix.out = mk_pp(0x991u, 0xD00Du, "away"); ix.in = mk_pp(0x992u, 7, "other");
        fb->notes[0] = ix; fb->notes[0].tank = 0xD00Du; fb->notes[1] = ix; fb->notes[1].tank = 0xF00Du; fb->notes[1].sid = 6;
        fb->notes[2] = ix; fb->notes[2].tank = 0xBEEFu; fb->notes[2].sid = 8;
        swap_save(fb); m_gone_tank = 0xD00Du;
        swap_boot(fb, &h, 0xBEEFu); swap_crash(fb);
        SCHECK(nnotes(fb) == 2 && !swap_note_of(fb, 0xD00Du, 0) && swap_note_of(fb, 0xF00Du, 0) && swap_note_of(fb, 0xBEEFu, 0),
               "R2-L28: the boot's sweep (%d notes, gone tank's %d)", nnotes(fb), swap_note_of(fb, 0xD00Du, 0) != NULL);
        m_gone_tank = 0;
        if (nnotes(fb) == 2) printf("selftest-swap: 7. R2-L28: a note of a tank the board no longer holds goes at the boot (a REMOVE or RESET cut short); a shelved tank's stays\n"); }

    {   /* Q1 (Strato 11-23: FINISH THE TRADE when the tanks meet again), the edges on the book alone */
        const int f1 = fails;
        swap_note_t gv; memset(&gv, 0, sizeof gv);
        gv.st = SWN_GAVEUP; gv.role = SWAP_ROLE_I; gv.applied = 1; gv.sid = 5; gv.peer = 7; gv.tank = 0xBEEFu;
        gv.out = mk_pp(0x991u, 0xBEEFu, "away"); gv.in = mk_pp(0x992u, 7, "other");
        passport_t bpp = gv.in; snprintf(bpp.name, sizeof bpp.name, "other2");   /* (b as R's note has it now) */
        #define Q1_BOOK() memset(M, 0, sizeof M); M[0].uid = 0xBEEFu; fb = &M[0].B; h = m_hostw(0); fb->notes[0] = gv; swap_save(fb); swap_boot(fb, &h, 0xBEEFu)
        swap_book_t *fb; swap_host_t h;
        /* a home: DONE finishes it - a goes, b (as R's note has it) comes, the SWAPPED card */
        Q1_BOOK(); M[0].f[M[0].nf++] = gv.out;
        swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_DONE, 5, 0x992u, 0, &bpp, 1000, 1.0);
        SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x991u) < 0 && m_find(&M[0], 0x992u) >= 0 && !strcmp(M[0].f[m_find(&M[0], 0x992u)].name, "other2") && fb->said.what == SWAP_SAID_SWAPPED,
               "Q1: a home, DONE: notes %d a %d b %d said %d", nnotes(fb), m_find(&M[0], 0x991u), m_find(&M[0], 0x992u), fb->said.what);
        /* a not home just now (a stand-in for next door / at a door): nothing yet; home: the next DONE finishes */
        Q1_BOOK(); M[0].f[M[0].nf++] = gv.out; m_home_away = 1;    /* (a next door just now) */
        swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_DONE, 5, 0x992u, 0, &bpp, 1000, 1.0);
        SCHECK(nst(fb) == SWN_GAVEUP && m_find(&M[0], 0x991u) >= 0 && m_find(&M[0], 0x992u) < 0, "Q1: a not home - the finish went ahead (note %d)", nst(fb));
        m_home_away = 0; swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_DONE, 5, 0x992u, 0, &bpp, 1100, 1.1);
        SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x991u) < 0 && m_find(&M[0], 0x992u) >= 0, "Q1: a home again - not finished");
        /* a sold meanwhile, a place free: b only. Then a sold and the place filled (full): b waits at R; a place again: done */
        Q1_BOOK();
        swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_DONE, 5, 0x992u, 0, &bpp, 1000, 1.0);
        SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x992u) >= 0, "Q1: a gone, a place - b not in");
        Q1_BOOK(); for (int k = 0; k < 5; k++) M[0].f[M[0].nf++] = mk_pp(0x500u + (uint32_t)k, 0xBEEFu, "own");
        swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_DONE, 5, 0x992u, 0, &bpp, 1000, 1.0);
        SCHECK(nst(fb) == SWN_GAVEUP && m_find(&M[0], 0x992u) < 0 && M[0].nf == 5, "Q1: full - b came in as a sixth (note %d, %d fish)", nst(fb), M[0].nf);
        M[0].nf--;
        swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_DONE, 5, 0x992u, 0, &bpp, 1100, 1.1);
        SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x992u) >= 0 && M[0].nf == 5, "Q1: a place again - b not in");
        /* a crash after TAKING is on the flash, before the fish change: the boot finishes it (never a, never b, never both) */
        Q1_BOOK(); M[0].f[M[0].nf++] = gv.out;
        fb->notes[0].st = SWN_TAKING; swap_save(fb); swap_boot(fb, &h, 0xBEEFu);
        SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x991u) < 0 && m_find(&M[0], 0x992u) >= 0, "Q1: TAKING at a boot not finished");
        /* R had no note (never applied, reset, its week over): its ABORT - the record goes, a stays home */
        Q1_BOOK(); M[0].f[M[0].nf++] = gv.out;
        swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_ABORT, 5, 0, 0, NULL, 1000, 1.0);
        SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x991u) >= 0 && m_find(&M[0], 0x992u) < 0, "Q1: ABORT on a GAVE-UP note (notes %d)", nnotes(fb));
        /* the partner never back: the record goes past R's week (R2-1's end; b is lost only with the partner truly gone) */
        Q1_BOOK(); M[0].f[M[0].nf++] = gv.out; m_wall_now[0] = 0; q_clk = 10; q_ms = 0;
        fb->notes[0].wait_s = SWAP_GAVEUP_S - 120;
        for (int k = 0; k < 100; k++) q_tick(fb, &h, 0xBEEFu, 1.0, false);
        SCHECK(nst(fb) == SWN_GAVEUP, "Q1: the record went before its time");
        for (int k = 0; k < 100; k++) q_tick(fb, &h, 0xBEEFu, 1.0, false);
        SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x991u) >= 0, "Q1: the record outlived R's week (notes %d)", nnotes(fb));
        /* a GAVE-UP note holds no fish: a may be sold, given or moved (what is done with it is lost at the finish); the bowl's LET IN never waits on it */
        Q1_BOOK(); M[0].f[M[0].nf++] = gv.out;
        SCHECK(!swap_holds(fb, 0x991u) && !swap_noted(fb, 0x991u, 0xBEEFu) && !swap_noted(fb, 0x992u, 0xBEEFu), "Q1: a GAVE-UP note holds a fish");
        #undef Q1_BOOK
        if (fails == f1) printf("selftest-swap: 7. Q1 on the book: a home - DONE finishes it; a away - it waits; a gone - b only; full - b waits at R; a cut mid-finish; ABORT and the week end let it go\n"); }
    {   /* the blobs older builds wrote (harness/SW): version 1 (11-21/22) and version 2 (11-22) still read, their notes whole */
        static const struct { const char *f; int n; uint32_t sid; uint32_t wait_s; } B[] = {
            { "v1-one.bin", 1, 77, 0 }, { "v1-one-fin2.bin", 1, 77, 0 }, { "v1-two-fin8.bin", 2, 77, 0 }, { "v1-other-only.bin", 1, 9, 0 },
            { "v2-1.bin", 1, 900, 6000 }, { "v2-2.bin", 2, 900, 6000 }, { "v2-3.bin", 3, 900, 6000 }, { "v2-4.bin", 4, 900, 6000 } };
        const int f1 = fails;
        for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++) {
            char path[160]; snprintf(path, sizeof path, "testdata/swap/%s", B[i].f);
            FILE *f = fopen(path, "rb"); if (!f) { snprintf(path, sizeof path, "sim/testdata/swap/%s", B[i].f); f = fopen(path, "rb"); }
            SCHECK(f != NULL, "blobs: %s not found", B[i].f); if (!f) continue;
            swap_book_t *bk = &M[1].B; memset(bk, 0, sizeof *bk);
            bk->mem_len = (int)fread(bk->mem, 1, sizeof bk->mem, f); fclose(f);
            swap_load(bk);
            const swap_note_t *g = n1(bk);
            SCHECK(nnotes(bk) == B[i].n && g && g->sid == B[i].sid && g->wait_s == B[i].wait_s && g->out.uid && g->in.uid,
                   "blobs: %s read %d notes (want %d), first sid %u wait %u", B[i].f, nnotes(bk), B[i].n, g ? g->sid : 0, g ? g->wait_s : 0);
            swap_save(bk); swap_book_t again; memset(&again, 0, sizeof again); memcpy(again.mem, bk->mem, (size_t)bk->mem_len); again.mem_len = bk->mem_len; swap_load(&again);
            SCHECK(nnotes(&again) == B[i].n && bk->mem[2] == 3, "blobs: %s written again as version 3 and read back (%d notes)", B[i].f, nnotes(&again));
        }
        if (fails == f1) printf("selftest-swap: 7. the blobs versions 1 and 2 wrote (8, from the audit's harness) still read whole, and write again as version 3\n"); }
}

/* the same on two real tanks: tank_link's host, the real link, the cards */
static int64_t r_wall_at[2];                         /* (each tank's wall clock; 0 = not set) */
static int64_t r_wall(const void *t) { return r_wall_at[t == &RB]; }
static void r_apart(double secs, bool boot_a, bool boot_b) {   /* both off `secs` (the wall clock runs), each booted after */
    for (int k = 0; k < 2; k++) if (r_wall_at[k]) r_wall_at[k] += (int64_t)secs;
    rnow += (uint32_t)(secs * 1000.0);
    if (boot_a) { swap_crash(&SA); tank_link_swap_boot(&RA); }
    if (boot_b) { swap_crash(&SB); tank_link_swap_boot(&RB); }
}
static void r_tick_wall(int frames) { for (int n = 0; n < frames; n++) { for (int k = 0; k < 2; k++) if (r_wall_at[k]) r_wall_at[k] += (n % 30 == 0); r_step(1); } }
static bool r_trade_no_closed(int ia, int ib, uint32_t *a, uint32_t *b) {   /* a trade whose CLOSED to B is lost: B keeps an applied note */
    *a = RA.fish[ia].uid; *b = RB.fish[ib].uid;
    render_swap_forget();
    if (!r_through(ia, ib)) return false;
    r_drop_kind_to[1] = SWK_CLOSED;
    r_tap(&RA, RENDER_SW_BTN_LEFT);
    for (int n = 0; n < 1500; n++) { if (r_slot(&RA, *b) >= 0 && r_slot(&RB, *a) >= 0 && nst(&SA) == SWN_NONE && SA.out.st == SO_OFF && SB.in.st == SI_OFF) break; r_step(1); }
    return r_slot(&RA, *b) >= 0 && r_slot(&RB, *a) >= 0 && nst(&SA) == SWN_NONE;
}
static int r3_other(const tank_t *t, uint32_t not1, uint32_t not2) { for (int i = 0; i < t->n_fish; i++) if (t->fish[i].uid != not1 && t->fish[i].uid != not2) return i; return -1; }
static void part_r2_real(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swapr2.sav", 1);
    tank_link_swap_hour = r_hour; r_hour_now = 500000; tank_link_swap_wall = r_wall;
    {   /* R2-1 (V1 c1): four trades, every CLOSED lost - B holds four notes and is BUSY. A day apart they give room: the fifth trade is
           taken (the note longest apart gives way); a week apart the rest go */
        const int f1 = fails; uint32_t a, b;
        r_wall_at[0] = r_wall_at[1] = 1800000000; r_setup(false); r_step(40);
        for (int k = 0; k < 4; k++) SCHECK(r_trade_no_closed(k, k, &a, &b), "R2-1: trade %d", k + 1);
        SCHECK(nnotes(&SB) == 4 && swap_busy(&SB), "R2-1: B's notes %d busy %d (want 4, 1)", nnotes(&SB), swap_busy(&SB));
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
        r_apart(26 * 3600.0, false, true); r_tick_wall(2200);
        SCHECK(nnotes(&SB) == 4 && swap_busy(&SB), "R2-1: a day apart, B notes %d busy %d (want 4 kept, still busy: an I off a day finds its note)", nnotes(&SB), swap_busy(&SB));
        for (int d = 0; d < 6; d++) { r_apart(86000.0, false, true); r_tick_wall(2200); }
        SCHECK(nnotes(&SB) == 4 && !swap_busy(&SB), "R2-1: a week apart, B notes %d busy %d (want 4 kept, room)", nnotes(&SB), swap_busy(&SB));
        RA.uid ^= 0x5A5A0000u; swap_crash(&SA); tank_link_swap_boot(&RA);   /* A's keeper RESET it: a new tank ID (it asks B nothing) */
        { link_host_t ha = tank_link_host(&RA, r_send); link_init(LA, RA.uid, &ha, 5, rnow); }
        v_relink(300);
        SCHECK(link_neighbor(LB, LINK_LEFT) == RA.uid, "R2-1: B never linked with the reset A");
        SCHECK(r_trade_no_closed(4, 4, &a, &b), "R2-1: the fifth trade (B was BUSY for good)");
        SCHECK(nnotes(&SB) == 4, "R2-1: after the fifth, B notes %d (want 4: one gave way)", nnotes(&SB));
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
        for (int d = 0; d < 31; d++) { r_apart(86000.0, false, true); r_tick_wall(2200); }
        SCHECK(nnotes(&SB) == 0 && !swap_busy(&SB), "R2-1: a month apart, B notes %d (want 0)", nnotes(&SB));
        r_down();
        if (fails == f1) printf("selftest-swap: 7. R2-1 on real tanks: four lost CLOSEDs - a week apart B takes a fifth swap (the partner came back reset), a month apart its notes are gone\n"); }
    {   /* LK-3 kept: A with no clock (its day is awake time) and B with one, apart 3 days asleep - at the next link a's COMMIT gets DONE */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = 0; r_wall_at[1] = 1800000000;
        SCHECK(v_stage_hard(3, &a0, &b0), "LK-3 (r2): staging");
        r_apart(3 * 86400.0, true, true); r_tick_wall(2200);
        SCHECK(nst(&SA) == SWN_COMMITTED && nst(&SB) == SWN_DONE, "LK-3 (r2): 3 days apart, notes A %d B %d (want COMMITTED, DONE)", nst(&SA), nst(&SB));
        v_relink(300);
        SCHECK(v_where(a0) == 2 && v_where(b0) == 1 && nnotes(&SA) == 0 && nnotes(&SB) == 0, "LK-3 (r2): after the link a in %d, b in %d (want 2, 1), notes %d/%d",
               v_where(a0), v_where(b0), nnotes(&SA), nnotes(&SB));
        r_down();
        if (fails == f1) printf("selftest-swap: 7. LK-3 kept: a late COMMIT (3 days apart, I with no clock) still gets DONE - the trade finishes\n"); }
    {   /* PW-6 with clocks: 23 h 59 apart, then a day off and A boots beside B - the wall clock says the day is over, the boot's minute lets the
           link finish it: no double. And with B never back, a comes home past the minute (the hard case) */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = r_wall_at[1] = 1800000000;
        SCHECK(v_stage_hard(4, &a0, &b0), "PW-6 (r2): staging");
        n1(&SA)->wait_s = SWAP_WAIT_S - 60; swap_save(&SA);
        r_apart(25 * 3600.0, true, true);
        tank_link_tick(&RA, NULL); r_step(3);
        SCHECK(nst(&SA) == SWN_COMMITTED && v_where(a0) == 2, "PW-6 (r2): the boot's first tick ended the note (note %d, a in %d)", nst(&SA), v_where(a0));
        v_relink(300);
        SCHECK(v_where(a0) == 2 && v_where(b0) == 1 && nnotes(&SA) == 0 && nnotes(&SB) == 0, "PW-6 (r2): after the link a in %d, b in %d", v_where(a0), v_where(b0));
        r_down();
        if (fails == f1) printf("selftest-swap: 7. PW-6 on real tanks with clocks: a day off counts, the boot beside B lets the link finish it - no double\n"); }

    {   /* Q1 (V1 c3): the hard case's day over - a home in A and with B (the double) - then the tanks meet: the trade finishes, a only in B,
           b in A, no notes, five and five. And with a sold meanwhile: b comes into the free place */
        for (int sold = 0; sold < 2; sold++) {
            const int f1 = fails; uint32_t a0, b0;
            r_wall_at[0] = r_wall_at[1] = 1800000000;
            SCHECK(v_stage_hard(3, &a0, &b0), "Q1: staging");
            n1(&SA)->wait_s = SWAP_WAIT_S - 60; n1(&SA)->part = 0;
            r_double_ok = true; r_tick_wall(2200);
            SCHECK(nst(&SA) == SWN_GAVEUP && v_where(a0) == 3 && v_where(b0) == 0, "Q1: the day: note %d, a in %d, b in %d (want GAVE-UP, both, none)", nst(&SA), v_where(a0), v_where(b0));
            if (sold) SCHECK(progression_sell_fish(&RA, r_slot(&RA, a0)), "Q1: a not sold");
            v_relink(600);
            SCHECK(v_where(a0) == 2 && v_where(b0) == 1 && nnotes(&SA) == 0 && nnotes(&SB) == 0 && RA.n_fish == POP_CAP && RB.n_fish == POP_CAP,
                   "Q1%s: the meeting: a in %d, b in %d (want 2, 1), notes %d/%d, fish %d/%d", sold ? " (a sold)" : "", v_where(a0), v_where(b0), nnotes(&SA), nnotes(&SB), RA.n_fish, RB.n_fish);
            r_double_ok = false; r_down();
            if (fails == f1) printf("selftest-swap: 7. Q1 on real tanks%s: the tanks meet after the hard case's day - the trade finishes, no double, no loss\n", sold ? " (a sold meanwhile)" : ""); }
    }

    {   /* review r2: the hard case, B (R) busy with three more notes, and A off three days - then A boots beside B: B kept its note
           (no room taken under a week), the late COMMIT gets DONE, the trade finishes. Never a double */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = r_wall_at[1] = 1800000000;
        SCHECK(v_stage_hard(3, &a0, &b0), "off 3 days: staging");
        for (int k = 1; k < SWAP_NOTES; k++) { SB.notes[k] = *n1(&SB); SB.notes[k].sid = 900 + (uint32_t)k; SB.notes[k].peer = 0x1234500u + (uint32_t)k; }
        swap_save(&SB);
        r_apart(3 * 86400.0, true, true);
        r_double_ok = true;                                 /* (A's day is over by its clock: a home, the designed double until they meet) */
        r_tick_wall(2200);                                  /* (B awake past its grace; A's first minute beside nobody: the doors closed) */
        SCHECK(nst(&SA) == SWN_COMMITTED || nst(&SA) == SWN_GAVEUP, "off 3 days: A's note %d", nst(&SA));
        SCHECK(swap_busy(&SB), "off 3 days: B gives a note's room under a week");
        v_relink(600);
        SCHECK(v_where(a0) == 2 && v_where(b0) == 1 && nnotes(&SA) == 0, "off 3 days: after the link a in %d, b in %d (want 2, 1), A notes %d", v_where(a0), v_where(b0), nnotes(&SA));
        r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 7. A off 3 days, B full of notes: B keeps A's note, the late COMMIT gets DONE - the trade finishes (review r2)\n"); }
    {   /* Q1, R's notes erased (swap_reset keeps B's ID here; a real reset gives a new one and A never reaches it): A's ask gets a plain ABORT, its record goes, the double stays (as
           decided). R's note ended by time instead: part 8, R3-1 (b) and (c) - b comes to A */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = r_wall_at[1] = 1800000000;
        SCHECK(v_stage_hard(2, &a0, &b0), "Q1 gone: staging");
        n1(&SA)->wait_s = SWAP_WAIT_S - 60; n1(&SA)->part = 0;
        r_double_ok = true; r_tick_wall(2200);
        swap_reset(&SB, 0);
        v_relink(600);
        SCHECK(nnotes(&SA) == 0 && v_where(a0) == 3, "Q1 gone: notes %d, a in %d (want 0, both)", nnotes(&SA), v_where(a0));
        r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 7. Q1 with R's notes erased: A's ask gets ABORT, its record goes, the double stays (b lost only with the partner truly gone)\n"); }
    {   /* R2-L15: the bowl says why for a fish in a swap - never IS BEING GIVEN */
        r_wall_at[0] = r_wall_at[1] = 1800000000; r_setup(false); r_step(40);
        SCHECK(r_through(0, 2), "R2-L15: no last yes");
        const int why = progression_move_why(&RA, r_slot(&RA, RA.fish[0].uid));
        SCHECK(why == BOWL_NO_SWAP, "R2-L15: the bowl's reason for a fish in a swap is %d (want BOWL_NO_SWAP %d)", why, BOWL_NO_SWAP);
        r_down();
        if (why == BOWL_NO_SWAP) printf("selftest-swap: 7. R2-L15: the bowl refuses a fish in a swap as CAN'T MOVE RIGHT NOW, not IS BEING GIVEN\n"); }
    tank_link_swap_wall = NULL; tank_link_swap_hour = NULL; r_wall_at[0] = r_wall_at[1] = 0;
}
/* ===================== 8. audit r3, R3-1: the two records of an unfinished trade end on their own clocks =====================
   The hard case: A (I) is in GAVE-UP or COMMITTED, B (R) holds an applied DONE note - b's only live copy. Whichever record ends
   first, the meeting after must not lose b: an answer to a trade a tank has no record of keeps the fish (never "drop it") */
static void r3_gaveup(int ib, uint32_t *a0, uint32_t *b0) {
    r_wall_at[0] = r_wall_at[1] = 1800000000;
    SCHECK(v_stage_hard(ib, a0, b0), "R3-1: staging");
    n1(&SA)->wait_s = SWAP_WAIT_S - 60; n1(&SA)->part = 0;
    r_double_ok = true; r_tick_wall(2200);
    SCHECK(nst(&SA) == SWN_GAVEUP && v_where(*a0) == 3 && v_where(*b0) == 0, "R3-1: the day: note %d, a in %d, b in %d", nst(&SA), v_where(*a0), v_where(*b0));
}
static void r3_reboot_links(void) {               /* (both boards booted after a long apart: their ms start again - the test's engines past
                                                       2^31 ms never link again, a board's restart at every boot) */
    rnow = 1000; rqn = 0;
    link_host_t ha = tank_link_host(&RA, r_send), hb = tank_link_host(&RB, r_send);
    link_init(LA, RA.uid, &ha, 11, rnow); link_init(LB, RB.uid, &hb, 12, rnow);
    swap_crash(&SA); tank_link_swap_boot(&RA); swap_crash(&SB); tank_link_swap_boot(&RB);
}
static void r3_part(void) {
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swapr3.sav", 1);
    tank_link_swap_hour = r_hour; r_hour_now = 500000; tank_link_swap_wall = r_wall;
    {   /* (a), V1 C2: A's clock set, B's lags - A's record ends first (its 32 days), B's note still young. They meet: B's DONE gets
           A's "not here", b comes home to B - full, so it waits there (THIS TANK IS FULL), never ends by time, comes in at a place */
        const int f1 = fails; uint32_t a0, b0;
        r3_gaveup(3, &a0, &b0);
        n1(&SA)->wait_s = SWAP_GAVEUP_S - 60; n1(&SA)->part = 0; r_tick_wall(2200);
        SCHECK(nnotes(&SA) == 0 && nst(&SB) == SWN_DONE, "R3-1 (a): A's record did not end first (A %d B %d)", nnotes(&SA), nst(&SB));
        v_relink(600);
        SCHECK(nnotes(&SA) == 0 && nst(&SB) == SWN_DONE && n1(&SB) && n1(&SB)->gave == 2 && v_where(a0) == 3 && v_where(b0) == 0,
               "R3-1 (a): the meeting, B full: B notes %d (st %d gave %d), a in %d, b in %d (want b waiting in B's note)", nnotes(&SB), nst(&SB), n1(&SB) ? n1(&SB)->gave : -1, v_where(a0), v_where(b0));
        SCHECK(SB.said.what == SWAP_SAID_FULL, "R3-1 (a): B's glass said %d (want THIS TANK IS FULL)", SB.said.what);
        r_shot(&RB, "r3_full_R");
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
        for (int d = 0; d < 40; d++) { r_apart(86000.0, false, true); r_tick_wall(300); }
        SCHECK(nst(&SB) == SWN_DONE, "R3-1 (a): B's waiting note ended by time (b lost)");
        SCHECK(progression_sell_fish(&RB, r3_other(&RB, a0, b0)), "R3-1 (a): B's sale");
        r_tick_wall(300);
        SCHECK(nnotes(&SB) == 0 && v_where(b0) == 2 && RB.n_fish == POP_CAP, "R3-1 (a): a place at B: notes %d, b in %d, B fish %d (want b home)", nnotes(&SB), v_where(b0), RB.n_fish);
        r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 8. R3-1 (a): A's record ended first - at the meeting b goes home to B (full: it waits for a place, never lost)\n"); }
    {   /* (a) with a place at B: b home at the meeting */
        const int f1 = fails; uint32_t a0, b0;
        r3_gaveup(2, &a0, &b0);
        n1(&SA)->wait_s = SWAP_GAVEUP_S - 60; n1(&SA)->part = 0; r_tick_wall(2200);
        SCHECK(progression_sell_fish(&RB, r3_other(&RB, a0, b0)), "R3-1 (a2): B's sale");
        v_relink(600);
        SCHECK(nnotes(&SA) == 0 && nnotes(&SB) == 0 && v_where(a0) == 3 && v_where(b0) == 2, "R3-1 (a2): notes %d/%d, a in %d, b in %d (want 0/0, both, B)",
               nnotes(&SA), nnotes(&SB), v_where(a0), v_where(b0));
        r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 8. R3-1 (a), a place at B: b home to B at the meeting\n"); }
    {   /* (b), V2 C4: a sold, A filled - A full when they meet. A's glass says so; the meeting restarts both counts (linked is never
           apart); a month apart B's note goes, A's record outlives it: at the next meeting B's "not here" ABORT gives A b's only copy -
           A still full, it waits (never ends by time) and comes in at a place */
        const int f1 = fails; uint32_t a0, b0;
        r3_gaveup(3, &a0, &b0);
        SCHECK(progression_sell_fish(&RA, r_slot(&RA, a0)), "R3-1 (b): a not sold");
        passport_t np = mk_pp(0x7777001u, 0x12345u, "newbie"); np.stage = STAGE_ADULT; np.age_s = STAGE_ADULT_AGE + 60;
        SCHECK(progression_gift_adopt(&RA, &np) >= 0 && RA.n_fish == POP_CAP, "R3-1 (b): A not full");
        n1(&SA)->wait_s = 20 * 86400; n1(&SB)->wait_s = 20 * 86400; swap_save(&SA); swap_save(&SB);   /* (twenty days apart before they meet) */
        v_relink(600);
        SCHECK(nst(&SA) == SWN_GAVEUP && nst(&SB) == SWN_DONE && v_where(b0) == 0, "R3-1 (b): the meeting, A full: notes %d/%d b in %d", nst(&SA), nst(&SB), v_where(b0));
        SCHECK(SA.said.what == SWAP_SAID_FULL, "R3-1 (b): A's glass said %d (want THIS TANK IS FULL)", SA.said.what);
        r_shot(&RA, "r3_full_I");
        for (int k = 0; k < 18182 * 3; k++) r_step(1);      /* (linked 30 minutes) */
        SCHECK(n1(&SA) && n1(&SA)->wait_s < 600 && n1(&SB) && n1(&SB)->wait_s < 600, "R3-1 (b): the meeting did not restart the counts (A %u B %u s)",
               n1(&SA) ? n1(&SA)->wait_s : 0, n1(&SB) ? n1(&SB)->wait_s : 0);
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
        for (int d = 0; d < 31; d++) { r_apart(86000.0, true, true); r_tick_wall(2200); }
        SCHECK(nst(&SA) == SWN_GAVEUP && nnotes(&SB) == 0, "R3-1 (b): a month apart: A %d B %d (want A's record, B's gone)", nst(&SA), nnotes(&SB));
        r3_reboot_links(); v_relink(600);
        SCHECK(nst(&SA) == SWN_GAVEUP && n1(&SA) && n1(&SA)->gave == 2 && v_where(b0) == 0, "R3-1 (b): met again, A full: A note %d gave %d, b in %d (want b waiting in A's note)",
               nst(&SA), n1(&SA) ? n1(&SA)->gave : -1, v_where(b0));
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
        for (int d = 0; d < 40; d++) { r_apart(86000.0, true, false); r_tick_wall(300); }
        SCHECK(nst(&SA) == SWN_GAVEUP, "R3-1 (b): A's waiting record ended by time (b lost)");
        SCHECK(progression_sell_fish(&RA, 0), "R3-1 (b): A's sale");
        r_tick_wall(300);
        SCHECK(nnotes(&SA) == 0 && v_where(b0) == 1 && RA.n_fish == POP_CAP, "R3-1 (b): a place at A: notes %d, b in %d, A fish %d (want b in A)", nnotes(&SA), v_where(b0), RA.n_fish);
        r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 8. R3-1 (b): A full when they meet - its glass says so, linked time restarts both counts, B's note gone first: b's only copy goes to A and waits for a place\n"); }
    {   /* B's clock ahead of A's (A no clock, short sittings): B's month over while A is still COMMITTED (a out). The meeting: B's "not
           here" ABORT - a home AND b in (a place made), never a home with b lost */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = r_wall_at[1] = 1800000000;
        SCHECK(v_stage_hard(1, &a0, &b0), "R3-1 (c): staging");
        r_wall_at[0] = 0;                                   /* (A: no clock - its day is awake time) */
        for (int d = 0; d < 31; d++) { r_apart(86000.0, true, true); r_tick_wall(300); }
        r_tick_wall(2200);
        SCHECK(nst(&SA) == SWN_COMMITTED && nnotes(&SB) == 0, "R3-1 (c): A %d B %d (want COMMITTED, B's month over)", nst(&SA), nnotes(&SB));
        SCHECK(progression_sell_fish(&RA, 0), "R3-1 (c): A's sale");
        r_double_ok = true; r3_reboot_links(); v_relink(600);
        SCHECK(nnotes(&SA) == 0 && v_where(a0) == 3 && v_where(b0) == 1, "R3-1 (c): notes %d, a in %d, b in %d (want 0, both, A)", nnotes(&SA), v_where(a0), v_where(b0));
        r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 8. R3-1 (c): B's note gone while A still waits (A's clock behind): a comes home and b comes in\n"); }
    {   /* review r3: B (R in the hard case, holding b's only copy) full of notes a week apart starts its OWN swap - its commit takes the
           hard note's room. Remembered too: when A's COMMIT comes, B says NOT_HERE and A keeps b's copy (was: plain ABORT, b lost) */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = r_wall_at[1] = 1800000000;
        SCHECK(v_stage_hard(3, &a0, &b0), "R3-1 own room: staging");
        const uint32_t C = 0xC0C0C0u, a_uid = RA.uid;      /* (the hard case's partner: tank C - A's glass plays it at the end) */
        swap_note_t sa = *n1(&SA); sa.tank = C; const uint32_t hsid = n1(&SB)->sid;
        n1(&SB)->peer = C;
        memset(SA.notes, 0, sizeof SA.notes); swap_save(&SA);
        for (int k = 1; k < SWAP_NOTES; k++) { SB.notes[k] = *n1(&SB); SB.notes[k].sid = 900 + (uint32_t)k; SB.notes[k].peer = 0x1234500u + (uint32_t)k; SB.notes[k].wait_s = SWAP_R_SPARE_S + 100; }
        SB.notes[0].wait_s = SWAP_R_SPARE_S + 200; swap_save(&SB);
        r_double_ok = true; r_tick_wall(2200);
        SCHECK(!swap_busy(&SB), "R3-1 own room: B busy (no week's room)");
        v_relink(300);
        SB.want_uid = RB.fish[0].uid; SB.want_peer = RA.uid; r_step(20);
        r_tap(&RA, RENDER_SW_BTN_LEFT); r_tap(&RA, RENDER_SW_BTN_TILE0); r_tap(&RA, RENDER_SW_BTN_LEFT); r_step(20);
        r_tap(&RB, RENDER_SW_BTN_LEFT);
        for (int k = 0; k < 1500 && (SB.out.st != SO_OFF || nnotes(&SA)); k++) r_step(1);
        bool kept = false; for (int k = 0; k < SWAP_NOTES; k++) kept |= SB.notes[k].st && SB.notes[k].sid == hsid;
        SCHECK(!kept && SB.n_ended >= 1, "R3-1 own room: B's own swap did not take the hard note's room (kept %d, ended %d)", kept, SB.n_ended);
        link_close(LA, LINK_RIGHT); link_close(LB, LINK_LEFT); r_step(60);
        RA.uid = C; SA.notes[0] = sa; swap_save(&SA); swap_crash(&SA); tank_link_swap_boot(&RA);   /* (C comes back) */
        { link_host_t ha = tank_link_host(&RA, r_send); link_init(LA, RA.uid, &ha, 13, rnow); }
        v_relink(600);
        SCHECK(nst(&SA) == SWN_GAVEUP && n1(&SA) && n1(&SA)->gave == SWAP_GAVE_ONLY, "R3-1 own room: A note %d gave %d, b in %d (want b's copy kept)", nst(&SA), n1(&SA) ? n1(&SA)->gave : -1, v_where(b0));
        RA.uid = a_uid; r_double_ok = false; r_down();
        if (fails == f1) printf("selftest-swap: 8. R3-1: B's own swap takes the hard note's week room - remembered, A keeps b's copy (review r3)\n"); }
    {   /* plain answers for what a tank did not end by time: A got b, every CLOSED lost, A's keeper sells b at once - B's next DONE gets a plain CLOSED,
           never "not here": b does not come back to life in B */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = r_wall_at[1] = 1800000000; r_setup(false); r_step(40);
        SCHECK(r_trade_no_closed(1, 1, &a0, &b0), "R3-1 plain (I): the trade");
        SCHECK(progression_sell_fish(&RA, r_slot(&RA, b0)), "R3-1 plain (I): b not sold");
        r_drop_kind_to[1] = 0; r_step(200);
        SCHECK(nnotes(&SB) == 0 && v_where(b0) == 0 && v_where(a0) == 2, "R3-1 plain (I): B notes %d, b in %d (want 0, sold: nowhere), a in %d", nnotes(&SB), v_where(b0), v_where(a0));
        r_down();
        if (fails == f1) printf("selftest-swap: 8. R3-1 plain answers: b sold the moment it came, CLOSED lost - B's DONE gets a plain CLOSED (no sold fish back to life)\n"); }
    {   /* ... and R's: A committed, every COMMIT lost until B's pick ran out, B's keeper sells b - the COMMIT then gets a plain ABORT
           (B knows this trade: never applied), a home, b not brought to A */
        const int f1 = fails; uint32_t a0, b0;
        r_wall_at[0] = r_wall_at[1] = 1800000000; r_setup(false); r_step(40);
        a0 = RA.fish[0].uid; b0 = RB.fish[2].uid;
        SCHECK(r_through(0, 2), "R3-1 plain (R): no last yes");
        r_drop_kind_to[1] = SWK_COMMIT;
        r_tap(&RA, RENDER_SW_BTN_LEFT);
        for (int k = 0; k < 3000 && !(nst(&SA) == SWN_COMMITTED && SB.in.st == SI_OFF); k++) r_step(1);
        SCHECK(nst(&SA) == SWN_COMMITTED && SB.in.st == SI_OFF, "R3-1 plain (R): A %d, B's pick %d (want COMMITTED, ended)", nst(&SA), SB.in.st);
        SCHECK(progression_sell_fish(&RB, r_slot(&RB, b0)), "R3-1 plain (R): b not sold");
        r_drop_kind_to[1] = 0; r_step(200);
        SCHECK(nnotes(&SA) == 0 && v_where(a0) == 1 && v_where(b0) == 0, "R3-1 plain (R): A notes %d, a in %d, b in %d (want 0, A, nowhere)", nnotes(&SA), v_where(a0), v_where(b0));
        r_down();
        if (fails == f1) printf("selftest-swap: 8. R3-1 plain answers: b sold after B's pick ran out, COMMIT lost - a plain ABORT, a home (no sold fish to A)\n"); }
    {   /* on the book: cuts inside the new steps, the ended list on the flash, a stale plain ABORT, the hold (review r3) */
        const int f1 = fails; swap_book_t *fb; swap_host_t h;
        const passport_t a = mk_pp(0x991u, 0xBEEFu, "a"), bb = mk_pp(0x992u, 7, "b");
        swap_note_t gv; memset(&gv, 0, sizeof gv);
        gv.st = SWN_GAVEUP; gv.role = SWAP_ROLE_I; gv.applied = 1; gv.gave = SWAP_GAVE_ONLY; gv.sid = 5; gv.peer = 7; gv.tank = 0xBEEFu; gv.out = a; gv.in = bb;
        #define R3_BOOK(note) memset(M, 0, sizeof M); M[0].uid = 0xBEEFu; m_crashes = false; fb = &M[0].B; h = m_hostw(0); fb->notes[0] = (note); swap_save(fb)
        /* i_bring cut after RECEIVING was saved: the boot brings b, the note goes */
        { swap_note_t n = gv; n.st = SWN_RECEIVING; R3_BOOK(n); M[0].f[M[0].nf++] = a;
          swap_crash(fb); swap_boot(fb, &h, 0xBEEFu);
          SCHECK(nnotes(fb) == 0 && m_find(&M[0], 0x992u) >= 0 && m_find(&M[0], 0x991u) >= 0, "R3-1 book: RECEIVING at a boot (notes %d)", nnotes(fb)); }
        /* r_home cut after b came in, before the note went: the tick drops the note, no second b, full or not */
        { swap_note_t n; memset(&n, 0, sizeof n); n.st = SWN_DONE; n.role = SWAP_ROLE_R; n.applied = 1; n.gave = SWAP_GAVE_ONLY; n.sid = 5; n.peer = 0x1234u; n.tank = 0xBEEFu; n.out = bb; n.in = a;
          R3_BOOK(n); for (int k = 0; k < 4; k++) M[0].f[M[0].nf++] = mk_pp(0x500u + (uint32_t)k, 0xBEEFu, "own"); M[0].f[M[0].nf++] = bb;
          swap_crash(fb); swap_boot(fb, &h, 0xBEEFu); swap_tick(fb, NULL, &h, 0xBEEFu, 1000, 10);
          SCHECK(nnotes(fb) == 0 && M[0].nf == 5, "R3-1 book: r_home cut (notes %d, fish %d)", nnotes(fb), M[0].nf); }
        /* the ended list: an R note's month ends, a crash, then I's COMMIT - still NOT_HERE (the record is on the flash) */
        { swap_note_t n; memset(&n, 0, sizeof n); n.st = SWN_DONE; n.role = SWAP_ROLE_R; n.applied = 1; n.sid = 5; n.peer = 0x1234u; n.tank = 0xBEEFu; n.out = bb; n.in = a;
          n.wait_s = SWAP_R_KEEP_S + 10; R3_BOOK(n); m_wall_now[0] = 0;
          swap_boot(fb, &h, 0xBEEFu); for (int k = 0; k < 400; k++) swap_tick(fb, NULL, &h, 0xBEEFu, 1000 + (uint32_t)k * 250, 10 + k * 0.25);
          SCHECK(nnotes(fb) == 0 && fb->n_ended == 1, "R3-1 book: the month end not remembered (notes %d, ended %d)", nnotes(fb), fb->n_ended);
          swap_crash(fb);
          SCHECK(fb->n_ended == 1 && fb->ended[0].sid == 5 && fb->ended[0].peer == 0x1234u && fb->ended[0].role == SWAP_ROLE_R, "R3-1 book: the ended list did not survive a crash (%d)", fb->n_ended);
          for (int k = 0; k < 12; k++) { swap_note_t e = n; e.sid = 100 + (uint32_t)k; fb->notes[0] = e; fb->notes[0].wait_s = SWAP_R_KEEP_S + 10; swap_boot(fb, &h, 0xBEEFu);
                                         fb->notes[0] = e; fb->notes[0].wait_s = SWAP_R_KEEP_S + 10;
                                         for (int q = 0; q < 400; q++) swap_tick(fb, NULL, &h, 0xBEEFu, 1000 + (uint32_t)q * 250, 10 + q * 0.25); }
          swap_crash(fb);
          SCHECK(fb->n_ended == SWAP_ENDED && fb->ended[SWAP_ENDED - 1].sid == 111, "R3-1 book: the list keeps the newest %d (%d, last %u)", SWAP_ENDED, fb->n_ended, fb->ended[SWAP_ENDED - 1].sid); }
        /* a stale plain ABORT on a GAVE_ONLY record: kept */
        { R3_BOOK(gv); M[0].f[M[0].nf++] = a; for (int k = 0; k < 4; k++) M[0].f[M[0].nf++] = mk_pp(0x500u + (uint32_t)k, 0xBEEFu, "own");
          swap_boot(fb, &h, 0xBEEFu);
          swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_ABORT, 5, 0, SWAP_ANS_PLAIN, NULL, 1000, 10);
          SCHECK(nst(fb) == SWN_GAVEUP, "R3-1 book: a plain ABORT dropped b's only copy"); }
        /* NOT_HERE on a COMMITTED note whose a never left on the flash: a home, not held while b waits (full) */
        { swap_note_t n = gv; n.st = SWN_COMMITTED; n.applied = 0; n.gave = 0; R3_BOOK(n); M[0].f[M[0].nf++] = a;
          for (int k = 0; k < 4; k++) M[0].f[M[0].nf++] = mk_pp(0x500u + (uint32_t)k, 0xBEEFu, "own");
          swap_rx(fb, NULL, &h, 0xBEEFu, 7, SWK_ABORT, 5, 0, SWAP_ANS_NOT_HERE, NULL, 1000, 10);
          SCHECK(nst(fb) == SWN_GAVEUP && n1(fb)->gave == SWAP_GAVE_ONLY && !swap_holds(fb, 0x991u) && m_find(&M[0], 0x991u) >= 0,
                 "R3-1 book: COMMITTED, NOT_HERE: note %d gave %d, a held %d", nst(fb), n1(fb) ? n1(fb)->gave : -1, swap_holds(fb, 0x991u)); }
        /* R3-L10: a finish done at a boot shows the live path's card, dated by the first tick */
        { static const struct { uint8_t st, role, applied, gave; int want; const char *nm; } C[] = {
              { SWN_TAKING, SWAP_ROLE_I, 1, 1, SWAP_SAID_SWAPPED, "TAKING" }, { SWN_RECEIVING, SWAP_ROLE_I, 1, 0, SWAP_SAID_SWAPPED, "RECEIVING" },
              { SWN_BACK, SWAP_ROLE_I, 1, 1, SWAP_SAID_NOT_FINISHED, "BACK (hard case)" }, { SWN_BACK, SWAP_ROLE_I, 1, 0, SWAP_SAID_NOT_HAPPENED, "BACK" },
              { SWN_DONE, SWAP_ROLE_R, 0, 0, SWAP_SAID_SWAPPED, "R's DONE not applied" } };
          for (unsigned c = 0; c < sizeof C / sizeof C[0]; c++) {
              swap_note_t n = gv; n.st = C[c].st; n.role = C[c].role; n.applied = C[c].applied; n.gave = C[c].gave;
              if (n.role == SWAP_ROLE_R) { n.out = bb; n.in = a; }
              R3_BOOK(n);
              if (C[c].st == SWN_TAKING || (C[c].st == SWN_RECEIVING) || n.role == SWAP_ROLE_R) M[0].f[M[0].nf++] = n.role == SWAP_ROLE_R ? bb : a;
              if (C[c].st == SWN_RECEIVING) M[0].nf = 0;
              swap_crash(fb); swap_boot(fb, &h, 0xBEEFu);
              swap_tick(fb, NULL, &h, 0xBEEFu, 1000, 123.0);
              SCHECK(fb->said.what == C[c].want && fb->said.at == 123.0, "R3-L10: %s at a boot: card %d at %.1f (want %d at 123)", C[c].nm, fb->said.what, fb->said.at, C[c].want);
          } }
        /* R3-L15: a clock a year ahead at a boot - R's applied note (b's only live copy) and I's GAVE-UP are still there past the boot's
           minute (one span counts under a week, not a week's room). (The week and the month still end: part 7's R2-1 counts them
           in daily boots) */
        { swap_note_t n; memset(&n, 0, sizeof n); n.st = SWN_DONE; n.role = SWAP_ROLE_R; n.applied = 1; n.sid = 5; n.peer = 0x1234u; n.tank = 0xBEEFu; n.out = bb; n.in = a;
          n.wall_at = 1800000000u; n.wait_s = 600;
          swap_note_t g = gv; g.gave = 1; g.sid = 6; g.wall_at = 1800000000u; g.wait_s = 600;
          R3_BOOK(n); fb->notes[1] = g; swap_save(fb);
          m_wall_now[0] = 1800000000 + 365LL * 86400; swap_crash(fb); swap_boot(fb, &h, 0xBEEFu);
          for (int k = 0; k < 4 * 180; k++) { m_wall_now[0] += (k % 4 == 0); swap_tick(fb, NULL, &h, 0xBEEFu, 1000 + (uint32_t)k * 250, 10 + k * 0.25); }
          SCHECK(nnotes(fb) == 2 && fb->n_ended == 0, "R3-L15: a year's jump ended a record (notes %d, ended %d)", nnotes(fb), fb->n_ended);
          SCHECK(fb->notes[0].wait_s < SWAP_R_SPARE_S, "R3-L15: the jump counted %u s (a week's room)", fb->notes[0].wait_s);
          m_wall_now[0] = 0; }
        #undef R3_BOOK
        if (fails == f1) printf("selftest-swap: 8. R3-1 on the book: cuts in the new steps finish at the boot, the ended list is on the flash (newest 8), a stale plain ABORT keeps b's copy, a never held\n"); }
    tank_link_swap_wall = NULL; tank_link_swap_hour = NULL; r_wall_at[0] = r_wall_at[1] = 0;
}

int selftest_swap(void) {
    fails = 0;
    part_blob();
    part_foreign();
    int seeds = getenv("SWAP_SOAK") ? atoi(getenv("SWAP_SOAK")) : 6;
    int bad = 0;
    for (int s = 1; s <= seeds; s++) bad += part_soak((uint32_t)s, 10, s > 3);
    if (seeds > 3 && !bad) printf("selftest-swap: 2. soak: %d seeds of 10 minutes each, OK\n", seeds);
    for (int plant = 1; plant <= 2; plant++) {           /* the planted bugs: each must fail the soak */
        const int f0 = fails; int hit = 0;
        swap_test_plant = plant; s_mute = true;
        for (int s2 = 1; s2 <= 8 && !hit; s2++) hit += part_soak(100 + (uint32_t)s2, 10, true) > 0;
        swap_test_plant = 0; fails = f0; s_mute = false;
        SCHECK(hit, "planted bug %d (%s) went unseen by the soak", plant, plant == 1 ? "a out before its note" : "R's note dropped at DONE, not at CLOSED");
        if (hit) printf("selftest-swap: 2. planted bug %d (%s): the soak fails, as it must\n", plant, plant == 1 ? "a out before its note" : "R's note dropped at DONE, not at CLOSED");
    }
    part_cards();
    part_notices();
    part_v3();
    part_chaos();
    part_r2();
    part_r2_real();
    r3_part();
    setenv("POCKET_TANK_SAVE", "/tmp/pocket-tank-selftest-swapcards.sav", 1);
    tank_link_swap_hour = r_hour; r_hour_now = 500000;
    part_visiting();
    {   const int f0 = fails; swap_test_plant = 3; s_mute = true;    /* planted bug 3: the ABORT before the visitor's bubbles */
        part_visiting();
        const bool hit = fails > f0; fails = f0; swap_test_plant = 0; s_mute = false;
        SCHECK(hit, "planted bug 3 (ABORT without the visitor's bubbles) went unseen");
        if (hit) printf("selftest-swap: 3. planted bug 3 (ABORT without the visitor's bubbles): case 3o fails, as it must\n");
    }
    tank_link_swap_hour = NULL;
    printf("selftest-swap: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
