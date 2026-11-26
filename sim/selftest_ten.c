#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_ten.c - --selftest-ten (0.4.3): THE SAVE'S FISH on every board.
 *
 *   1. a tank filled to its cap (five on a pocket tank, ten on a big screen: POP_CAP) comes back fish for fish
 *      after a save and a boot - names, kinds, forms, genes' look, IDs, parents, ages, the one next door;
 *   2. a 0.4.2 pocket tank's save (the 1.8's, 2200 bytes: fixtures/save_042_rect.h) loads on every board;
 *   3. the big screens only: an October home-tank save (branch home-tank, ten fish, 3056 bytes, its fish tail
 *      at 1912: fixtures/save_oct_p4.h) loads with its ten fish, nothing of the old tail read as the shelf,
 *      the lock, the crab, the gifts or the stories; and is written back in today's layout.
 * The fixtures are whole saves made by those builds' own simulators (tools: see docs/home-tank-0.4.3.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "progression.h"
#include "fixtures/save_042_rect.h"
#ifdef TANK_P4
#include "fixtures/save_oct_p4.h"
#endif

static int fails_t;
#define TCHECK(cond, ...) do { if (!(cond)) { if (fails_t < 16) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } fails_t++; } } while (0)
static const char *SAV = "/tmp/pocket-tank-selftest-ten.sav";
static bool put_file(const unsigned char *b, size_t n) { FILE *f = fopen(SAV, "wb"); if (!f) return false; bool ok = fwrite(b, 1, n, f) == n; fclose(f); return ok; }
static long file_len(void) { FILE *f = fopen(SAV, "rb"); if (!f) return -1; fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return n; }

int selftest_ten(void) {
    setenv("POCKET_TANK_SAVE", SAV, 1);
    if (getenv("TEN_SAVE")) {                                        /* TEN_SAVE=<a board's save, as its backup holds it>: what this build makes of it (a copy: the file is not written) */
        static unsigned char buf[8192]; FILE *f = fopen(getenv("TEN_SAVE"), "rb"); size_t n = f ? fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f);
        if (!n || !put_file(buf, n)) { printf("selftest-ten: cannot read %s\n", getenv("TEN_SAVE")); return 1; }
        static tank_t X; tank_init(&X, 11); const float h = progression_boot(&X);
        printf("selftest-ten: %s (%u bytes) -> tank %08x, %d fish, %d sand dollars, unlocks %08x, shelved %08x, setup owed %d, %.1f h lived at this boot\n", getenv("TEN_SAVE"), (unsigned)n,
               X.uid, X.n_fish, (int)X.sd_balance, (unsigned)X.sd_unlocks, (unsigned)X.sd_shelved, (int)progression_setup_pending(), h);
        for (int i = 0; i < X.n_fish; i++) printf("selftest-ten:   %d %-8s %08x stage %d age %.1f h%s\n", i, X.fish[i].name, X.fish[i].uid, (int)X.fish[i].stage, progression_age_s(&X, i) / 3600, X.fish[i].away ? " (next door)" : "");
        const bool ok = progression_save(&X); printf("selftest-ten: saved again: %s, %ld bytes\n", ok ? "yes" : "REFUSED", file_len());
        return X.n_fish >= 2 && ok ? 0 : 1;
    }
    /* 1. the cap's fish, there and back */
    {   persist_port_erase();
        static tank_t A, B, K;
        progression_reset(&A, 900); progression_setup_done(&A);
        while (A.n_fish < POP_CAP && tank_add_fish(&A, 0, 1) >= 0) {}
        tank_ensure_ids(&A);
        TCHECK(A.n_fish == POP_CAP, "the tank grew to %d, its cap is %d", A.n_fish, POP_CAP);
        for (int i = 0; i < A.n_fish; i++) {
            char nm[FISH_NAME_MAX + 1]; snprintf(nm, sizeof nm, "F%02d", i);
            tank_set_name(&A, i, nm);
            A.fish[i].shape = (uint8_t)(i % SHAPE_N); A.fish[i].fins = (uint8_t)((i / 2) % FINS_N); A.fish[i].pattern = (uint8_t)(i % PATTERN_N);
            A.fish[i].ms_bits |= MS_ARRIVED; A.fish[i].ms_seen = A.fish[i].ms_bits;
            A.fish[i].drift_acc = 0.25f * (i + 1);
            A.sd_paid_fish[i] = 100u + (uint32_t)i;
            progression_set_age(&A, i, 100.0f * (i + 1));
            A.fish[i].stage = STAGE_ELDER;                                    /* (rel-0.4.3 SV-5: only an elder keeps a part at the load) */
            A.fish[i].evo = (uint8_t)(1 + i % (EVO_N - 1));                 /* (0.4.3: every fish a paragon, past the head's six too) */
            tank_set_body(&A.fish[i], 14.0f + i, 8.0f + 0.3f * i);           /* (... and its own body values) */
        }
        const int away = A.n_fish - 2; A.fish[away].away = true;      /* one next door: past the head's six on a big screen */
        TCHECK(progression_save(&A), "the save was refused");
        K = A;
        const long len = file_len();
#ifdef TANK_P4
        TCHECK(len == 3336, "a big screen's save is %ld bytes, not 3336 (0.4.3: the tail's paragon words and body values)", len);
#else
        TCHECK(len == 2304, "a pocket tank's save is %ld bytes, not 2304 (0.4.3's)", len);
#endif
        tank_init(&B, 3); progression_boot(&B);
        TCHECK(B.n_fish == POP_CAP && B.uid == K.uid, "came back %d fish, tank %08x (was %d, %08x)", B.n_fish, B.uid, K.n_fish, K.uid);
        for (int i = 0; i < B.n_fish && i < K.n_fish; i++) {
            const fish_t *a = &K.fish[i], *b = &B.fish[i];
            TCHECK(!strcmp(a->name, b->name) && a->preset == b->preset && a->uid == b->uid && a->parent_uid[0] == b->parent_uid[0] && a->parent_uid[1] == b->parent_uid[1],
                   "slot %d: name / kind / IDs ('%s' %d %08x -> '%s' %d %08x)", i, a->name, a->preset, a->uid, b->name, b->preset, b->uid);
            TCHECK(a->shape == b->shape && a->fins == b->fins && a->pattern == b->pattern && a->color == b->color && a->accent == b->accent && !memcmp(a->carried, b->carried, sizeof a->carried),
                   "slot %d: the form, the colors or the carried genes moved", i);
            TCHECK(fabsf(progression_age_s(&B, i) - 100.0f * (i + 1)) < 1.0f, "slot %d: age %.0f, want %.0f", i, progression_age_s(&B, i), 100.0f * (i + 1));
            TCHECK(b->away == (i == away), "slot %d: next door %d, want %d", i, b->away, i == away);
            TCHECK(b->parent_a == a->parent_a && b->parent_b == a->parent_b, "slot %d: parents by slot %d/%d, want %d/%d", i, b->parent_a, b->parent_b, a->parent_a, a->parent_b);
            TCHECK(b->ms_bits == a->ms_bits && b->ms_seen == a->ms_seen && b->drift_acc == a->drift_acc, "slot %d: its badges or its drift moved", i);
            TCHECK(B.sd_paid_fish[i] == 100u + (uint32_t)i, "slot %d: paid stages %u, want %u", i, B.sd_paid_fish[i], 100u + (uint32_t)i);
            TCHECK(b->evo == a->evo && !memcmp(b->body, a->body, sizeof a->body), "slot %d: its paragon part %d (want %d) or its body values moved", i, b->evo, a->evo);
        }
        printf("selftest-ten: 1. %d fish (the cap) saved in %ld bytes and back, fish for fish - one of them next door in slot %d\n", POP_CAP, len, away);
    }
    /* 2. a 0.4.2 pocket tank's save */
    {   static tank_t C;
        TCHECK(sizeof SAVE_042_RECT == 2200, "the 0.4.2 fixture is %u bytes", (unsigned)sizeof SAVE_042_RECT);
        TCHECK(put_file(SAVE_042_RECT, sizeof SAVE_042_RECT), "the fixture was not written");
        tank_init(&C, 4); progression_boot(&C);
        TCHECK(C.n_fish == SAVE_042_RECT_FISH && C.uid == SAVE_042_RECT_UID, "the 0.4.2 save came back %d fish, tank %08x (want %d, %08x)", C.n_fish, C.uid, SAVE_042_RECT_FISH, (unsigned)SAVE_042_RECT_UID);
        for (int i = 0; i < SAVE_042_RECT_FISH && i < C.n_fish; i++) TCHECK(C.fish[i].uid == SAVE_042_RECT_UIDS[i], "the 0.4.2 save's fish %d has ID %08x, want %08x", i, C.fish[i].uid, (unsigned)SAVE_042_RECT_UIDS[i]);
        TCHECK(C.sd_balance == SAVE_042_RECT_BALANCE && C.sd_unlocks == SAVE_042_RECT_UNLOCKS, "its wallet %d / unlocks %08x (want %d / %08x)", (int)C.sd_balance, (unsigned)C.sd_unlocks, SAVE_042_RECT_BALANCE, (unsigned)SAVE_042_RECT_UNLOCKS);
        TCHECK(progression_save(&C), "the 0.4.2 tank's next save was refused");
        static tank_t D; tank_init(&D, 5); progression_boot(&D);
        TCHECK(D.n_fish == C.n_fish && D.uid == C.uid && D.sd_balance >= C.sd_balance, "saved by this build and loaded again: %d fish, tank %08x", D.n_fish, D.uid);
        printf("selftest-ten: 2. a 0.4.2 pocket tank's save (2200 bytes, %d fish) loads here, is saved in %ld bytes and loads again\n", SAVE_042_RECT_FISH, file_len());
    }
#ifdef TANK_P4
    /* 3. an October home-tank save */
    {   static tank_t E, F;
        TCHECK(sizeof SAVE_OCT_P4 >= 3052 && sizeof SAVE_OCT_P4 <= 3120, "the October fixture is %u bytes", (unsigned)sizeof SAVE_OCT_P4);
        TCHECK(put_file(SAVE_OCT_P4, sizeof SAVE_OCT_P4), "the fixture was not written");
        tank_init(&E, 6); progression_boot(&E);
        TCHECK(E.n_fish == SAVE_OCT_P4_FISH && E.uid == SAVE_OCT_P4_UID, "the October save came back %d fish, tank %08x (want %d, %08x)", E.n_fish, E.uid, SAVE_OCT_P4_FISH, (unsigned)SAVE_OCT_P4_UID);
        for (int i = 0; i < SAVE_OCT_P4_FISH && i < E.n_fish; i++) {
            TCHECK(E.fish[i].uid == SAVE_OCT_P4_UIDS[i], "the October save's fish %d has ID %08x, want %08x", i, E.fish[i].uid, (unsigned)SAVE_OCT_P4_UIDS[i]);
            TCHECK(!SAVE_OCT_P4_NAMES[i][0] || !strcmp(E.fish[i].name, SAVE_OCT_P4_NAMES[i]), "the October save's fish %d is '%s', want '%s'", i, E.fish[i].name, SAVE_OCT_P4_NAMES[i]);
        }
        TCHECK(E.sd_balance == SAVE_OCT_P4_BALANCE, "its wallet %d, want %d", (int)E.sd_balance, SAVE_OCT_P4_BALANCE);
        TCHECK(E.sd_shelved == 0 && E.sd_locked == 0 && E.bg_now == 0 && E.breed_a == 0 && E.breed_b == 0 && E.sdg.out.amount == 0 && E.cr_found == 0,
               "the old fish tail was read as the shelf / the lock / the crab / the gifts (shelved %08x locked %08x bg %d crab %d gift %d)", (unsigned)E.sd_shelved, (unsigned)E.sd_locked, E.bg_now, (int)E.cr_found, (int)E.sdg.out.amount);
        TCHECK((E.sd_unlocks & ~SAVE_OCT_P4_UNLOCKS) == 0, "unlocks %08x: more than the October tank had (%08x)", (unsigned)E.sd_unlocks, (unsigned)SAVE_OCT_P4_UNLOCKS);
        TCHECK(progression_save(&E), "the October tank's next save was refused");
        TCHECK(file_len() == 3336, "written back in %ld bytes, not today's 3336", file_len());
        tank_init(&F, 7); progression_boot(&F);
        TCHECK(F.n_fish == E.n_fish && F.uid == E.uid, "in today's layout it came back %d fish, tank %08x", F.n_fish, F.uid);
        for (int i = 0; i < F.n_fish && i < E.n_fish; i++) TCHECK(!strcmp(F.fish[i].name, E.fish[i].name) && F.fish[i].uid == E.fish[i].uid, "slot %d changed in today's layout ('%s' -> '%s')", i, E.fish[i].name, F.fish[i].name);
        printf("selftest-ten: 3. an October home-tank save (%u bytes, %d fish, its fish tail at 1912) loads fish for fish, nothing of it read as a newer field; written back in 3272 bytes and loaded again\n", (unsigned)sizeof SAVE_OCT_P4, SAVE_OCT_P4_FISH);
    }
    {   static tank_t G;                                             /* (a staged tank of fifteen: the slots this tank has, never a new tank) */
        TCHECK(put_file(SAVE_OCT_P4_15, sizeof SAVE_OCT_P4_15), "the fixture was not written");
        tank_init(&G, 8); progression_boot(&G);
        TCHECK(G.n_fish == N_FISH_MAX && G.uid == SAVE_OCT_P4_15_UID, "an October save of fifteen came back %d fish, tank %08x (want %d, %08x)", G.n_fish, G.uid, N_FISH_MAX, (unsigned)SAVE_OCT_P4_15_UID);
        for (int i = 0; i < G.n_fish; i++) TCHECK(G.fish[i].uid == SAVE_OCT_P4_15_UIDS[i], "its fish %d has ID %08x, want %08x", i, G.fish[i].uid, (unsigned)SAVE_OCT_P4_15_UIDS[i]);
        printf("selftest-ten: 3b. an October save with all fifteen slots filled loads its first %d fish, the same tank\n", N_FISH_MAX);
    }
#endif
    printf("selftest-ten: %s\n", fails_t ? "FAILED" : "OK");
    return fails_t ? 1 : 0;
}
