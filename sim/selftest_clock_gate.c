/* selftest_clock_gate.c - --selftest-clock-gate (0.4.1): THE TIME CHECK, boot by boot.
 *
 * The boot's ask for the time (firmware main.c net_clock_boot) is not in the sim, but its gate is common/clock_gate.h,
 * and this is a host model of the rest of that boot, re-derived from main.c and net_port_esp.c: the chip read
 * (stopped / no answer / running), the gate, the try (the router up, not seen, seen but slow, or missed by a quick
 * look), what the try writes (netok, netgave, netlag, the Wi-Fi namespace's miss), progression_boot / _wake living
 * the clock minus the save's stamp, the first sync's rebase (nothing lived for the jump), the cold boot's seed at the
 * save's stamp (the stretch owed), and the nights (a chip-board's power-off; the pendant's power-off only after a
 * good try, else a deep sleep with the clock running). It is the yardstick "clk4" (release-0.4.md 2g item 1),
 * rebuilt, with the cases that found every earlier skip's hole, and run three ways: 0.4.0's gate (= 0.3.9.6's),
 * 0.4.1 the first boots after the OTA (no netlag yet), and 0.4.1 settled (netlag 0 from a good sync).
 *
 * The rule it holds 0.4.1 to: in EVERY case the owed time left at the end is no more than 0.4.0's, and none in the
 * cases where the network comes back - a night never lived is worse than a slow boot - and never MORE lived than the
 * time away (a jump paid twice; one boot lives 7 days at most, as progression does). Then: less TIME CHECK time than
 * 0.4.0 (a case flagged MORE may cost a page more: a payment delayed by a quick look's miss or a cut, or made sooner). And over every input of the gate:
 * 0.4.1 skips where 0.4.0 asks only when nothing is owed, and asks where 0.4.0 skips only when something is.
 *
 * Cost of a page in the model: a good try 5 s, a failed try 30 s, a failed quick look 7 s (net_port_esp.c: the
 * learned channel, a 1 s pause, one sweep of them all; Strato's stopwatch put an empty sweep at ~2-4 s).
 * `--selftest-clock-gate` prints the summary; PT_CLOCK_TRACE=1 prints every boot. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clock_gate.h"

enum { FW040, FW041, FWCBD };   /* FWCBD: cbd63a8's skip (0.4 rc round one, reverted): the model must see its hole */
enum { R_UP, R_DOWN, R_SLOW, R_COLDMISS, R_WEAK, R_NONE };   /* the router: there; not seen; seen, never answering in 30 s; there, but a quick look's
                                                               sweeps miss it (once); at the edge of range: only a full look finds it, every time */
enum { B_COLD, B_WAKE, B_SOFT };
#define ABSENT 255
#define BUILD_T (10000.0 - 720)                        /* the build time: older than any save (0.4.1 audit: a month before the cases start, so a seed at it shows) */
#define CAP_H 168.0                                    /* PROGRESSION_SLEEP_CAP_S: one boot lives 7 days at most */

typedef struct {
    int fw;
    bool chip;                       /* a clock chip (the 1.8, the watch, the square); false: the pendant */
    bool chip_run; double chip_t;    /* the chip's time while it runs */
    bool sys_set; double sys_t;      /* the ESP's own clock: through deep sleep and soft restarts, not a power-off */
    bool saved;                      /* a network saved */
    int netclk, netok, netgave, netlag, miss;
    double S, real, down_at;         /* the save's stamp (clock h), real time (h), when the tank went down */
    int next;                        /* the next boot's kind */
    bool unread_next, cut_in_seed, cut_in_sync;
    bool noprobe_next, stamp_lost_next;  /* the chip missed the probe at this boot (s_rtc false: Q); a cut swap finished by shelf_boot(0) (X) */
    double mark_away, mark_lived;        /* a snapshot at the restore (P) */
    int moved;                           /* the cold boot moved a lagging clock up to the save's stamp (W3) */
    double clk; bool set;
    double away, lived, secs;
    int pages, quick;
    bool trace;
} W;

static clock_gate_t clock_gate_cbd(const clock_gate_in_t *g) {   /* `real && set && (ok_before || s_rtc)` */
    if (g->real && g->set && (g->ok_before || g->rtc)) return CLOCK_SKIP_GOOD;
    return clock_gate_040(g);
}
static void wput(W *w, const char *label, const char *what) {
    if (!w->trace) return;
    printf("    %-52s %-11s lived %5.1f h | owed %5.1f h | lag %-3s ok %d miss %d\n", label, what, 0.0, w->away - w->lived,
           w->netlag == ABSENT ? "-" : w->netlag ? "1" : "0", w->netok, w->miss);
}

static void init(W *w, int fw, bool chip, bool trace) {
    memset(w, 0, sizeof *w);
    w->fw = fw; w->chip = chip; w->trace = trace;
    w->real = 10000; w->chip_run = chip; w->chip_t = w->real; w->sys_set = true; w->sys_t = w->real;
    w->saved = true; w->netclk = 1; w->netok = 1; w->netgave = 0; w->netlag = ABSENT; w->miss = 0;
    w->clk = w->real; w->set = true; w->S = w->real; w->down_at = w->real; w->next = B_COLD;
}
static void awake(W *w, double h) {                  /* the tank's day: lived as it passes, saved as it goes */
    w->real += h; w->clk += h; if (w->chip_run) w->chip_t += h; w->sys_t = w->clk; w->S = w->clk;
}
static bool night_powers_off(const W *w) { return w->chip || (w->saved && w->netclk && w->netok); }   /* main.c (s_pmic on every board) */
static void night(W *w, double h, bool stop) {       /* the tank goes down at bedtime and is away h hours */
    w->S = w->clk; w->down_at = w->real;
    bool off = night_powers_off(w) || stop;
    w->real += h;
    if (w->chip) { if (stop) w->chip_run = false; else w->chip_t += h; }
    if (off) { w->sys_set = false; w->next = B_COLD; } else { w->sys_t += h; w->next = B_WAKE; }
}
static void power_back(W *w, double h) {             /* after a cut mid-boot: nothing was saved, the save keeps its stamp */
    w->real += h; if (w->chip_run) w->chip_t += h;
}
static void restart(W *w) {                          /* a soft restart: SWAP IN, the Wi-Fi setup, an update */
    w->S = w->clk; w->down_at = w->real;
    w->real += 0.01; w->sys_t += 0.01; if (w->chip_run) w->chip_t += 0.01;
    w->next = B_SOFT;
}

static void boot(W *w, int router, const char *label) {
    int kind = w->next;
    w->away += w->real - w->down_at;
    bool unread = false;
    const bool rtc = w->chip && !w->noprobe_next;     /* s_rtc at this boot */
    if (w->chip && w->noprobe_next) { w->noprobe_next = false; w->set = kind != B_COLD && w->sys_set; w->clk = w->sys_t; }
    else
    /* rtc_port_init: the chip's time sets the clock; a stopped chip, or one that did not answer, leaves it as it is */
    if (w->chip && w->unread_next) { unread = true; w->unread_next = false; w->set = kind != B_COLD && w->sys_set; w->clk = w->sys_t; }
    else if (w->chip && !w->chip_run) w->set = false;
    else if (w->chip) { w->set = true; w->clk = w->chip_t; }
    else { w->set = kind != B_COLD && w->sys_set; w->clk = w->sys_t; }
    /* net_clock_boot */
    char what[16] = "";
    bool rebase = false;
    if (w->saved) {
        const clock_gate_in_t g = { .rtc = rtc, .real = w->netclk, .set = w->set, .ok_before = w->netok,
                                    .lag = w->fw == FW041 ? clock_lag_of(w->netlag) : CLOCK_LAG_UNKNOWN, .gave = w->netgave,
                                    .missed = w->fw == FW041 ? w->miss : 0 };
        const clock_gate_t d = w->fw == FW041 ? clock_gate(&g) : w->fw == FWCBD ? clock_gate_cbd(&g) : clock_gate_040(&g);
        if (clock_gate_asks(d)) {
            const bool quick = d == CLOCK_ASK_QUICK;
            bool ok = false, seen = false;
            if (router == R_UP) { ok = seen = true; w->secs += 5; }
            else if (router == R_DOWN) w->secs += quick ? 7 : 30;
            else if (router == R_SLOW) { seen = true; w->secs += 30; }
            else if (router == R_COLDMISS || router == R_WEAK) { ok = seen = !quick; w->secs += quick ? 7 : 5; }
            w->pages++; w->quick += quick;
            snprintf(what, sizeof what, "%s ok=%d", quick ? "QUICK" : "PAGE ", ok);
            if (w->fw == FW041) { if (ok || seen) w->miss = 0; else w->miss++; }   /* net_port_esp.c: a connect / a router seen clears it */
            if (!ok) w->netok = 0;
            if (rtc && w->set && !w->netclk) w->netgave = ok ? 0 : w->netgave + 1;
            if (ok && w->netclk) {
                w->clk = w->real; w->set = true;
                if (rtc) { w->chip_run = true; w->chip_t = w->real; }      /* rtc_port_store */
                if (w->cut_in_sync) {                        /* a power cut right after the chip's write (0.4.0 had written netok 1 before it; 0.4.1 writes netlag 0 + netok 1 after) */
                    if (w->fw == FW040) w->netok = 1;
                    w->cut_in_sync = false; w->down_at = w->real; w->sys_set = false; w->next = B_COLD; wput(w, label, "CUT");
                    return;
                }
                if (w->fw == FW041) w->netlag = 0;
                w->netok = 1;
            } else if (ok) { w->netok = 1; rebase = true; }
        } else snprintf(what, sizeof what, "%s", d == CLOCK_SKIP_RAN ? "(ran)" : d == CLOCK_SKIP_GAVE_UP ? "(gave up)" : "");
    }
    /* shelf_boot(clock_port_now_unix()) runs here, before the save loads: a cut swap finished with no clock stamped the
       incoming save 0 - banked (0.4.0; the seed then started the clock at the BUILD time). 0.4.1 stamps it at the swap's
       own moment (shelf.c, audit W1): the save's stamp stands */
    if (w->stamp_lost_next) { w->stamp_lost_next = false; if (!w->set && w->fw != FW041) w->S = 0; }
    /* progression_boot / _wake: the clock minus the save's stamp */
    double h = w->set && w->clk > w->S ? w->clk - w->S : 0;
    if (h > CAP_H) h = CAP_H;                        /* PROGRESSION_SLEEP_CAP_S */
    w->lived += h;
    if (rebase) {                                    /* net_clock_rebase: the clock jumps to real time, nothing lived for it */
        w->clk = w->real; w->set = true; if (w->chip) { w->chip_run = true; w->chip_t = w->real; }
        w->netclk = 1; if (w->fw == FW041) { w->netlag = 0; w->netok = 1; }
        w->S = w->clk;                               /* (progression_settings_changed: the save stamps the new clock) */
    }
    if (kind != B_WAKE && !w->set) {                 /* the cold boot's seed: the clock resumes at the save, the stretch owed */
        w->clk = w->S > BUILD_T ? w->S : BUILD_T; w->set = true;
        if (!unread) {
            if (w->fw == FW041) { w->netlag = 1; w->netok = 0; }
            if (w->cut_in_seed) {                    /* a power cut before the chip's write */
                w->cut_in_seed = false; w->down_at = w->real; w->sys_set = false; w->next = B_COLD;
                if (w->trace) printf("    %-52s %-11s lived %5.1f h | owed %5.1f h | (cut in the seed)\n", label, what, h, w->away - w->lived);
                return;
            }
            if (rtc) { w->chip_run = true; w->chip_t = w->clk; }
        }
    }
    /* 0.4.1 (main.c, the cold boot, audit W3): a clock that reads BEHIND the loaded save's stamp is moved up to it - only a
       restore stamps a save ahead of the board's clock (the page stamps it at the computer's now) - and only while a sync is
       still owed (netlag 1, or not known with the last try failed): the gate asks again at every boot, so a computer's clock
       that ran ahead is put right by the next good sync, never kept */
    if (w->fw == FW041 && kind != B_WAKE && w->set && w->S > w->clk &&
        (clock_lag_of(w->netlag) == CLOCK_LAG_OWED || (clock_lag_of(w->netlag) == CLOCK_LAG_UNKNOWN && !w->netok))) {
        w->clk = w->S; if (rtc && !unread) { w->chip_run = true; w->chip_t = w->clk; }   /* (a chip that would not answer is never written) */
        w->moved++; snprintf(what + strlen(what), sizeof what - strlen(what), " UP");
    }
    w->sys_set = true; w->sys_t = w->clk;
    if (w->trace) printf("    %-52s %-11s lived %5.1f h | owed %5.1f h | lag %-3s ok %d miss %d\n", label, what, h, w->away - w->lived,
                         w->netlag == ABSENT ? "-" : w->netlag ? "1" : "0", w->netok, w->miss);
}

/* ---------------- the cases ---------------- */
#define DAY 14.0
#define NIGHT 10.0
typedef void (*case_fn)(W *w, int variant);
enum { V040, V041_OTA, V041_SET, NV };
static const char *VNAME[NV] = { "0.4.0 (0.3.9.6's gate)", "0.4.1, off the OTA (no netlag)", "0.4.1, settled (netlag 0)" };
static void morning(W *w, int router, const char *label) { awake(w, DAY); night(w, NIGHT, false); boot(w, router, label); }

static void cA(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot (8 h), router still booting");
    awake(w, 0.17); w->S = w->clk; w->down_at = w->real; w->next = B_COLD; boot(w, R_UP, "a power-cycle 10 min on, router up");
    morning(w, R_UP, "next morning"); }
static void cB(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down");
    morning(w, R_DOWN, "morning, router down"); morning(w, R_UP, "morning, router back"); morning(w, R_UP, "morning"); }
static void cC(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, no network");
    for (int i = 1; i <= 5; i++) morning(w, R_DOWN, "morning, no network (gone for good)"); }
static void cC2(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, no network");
    for (int i = 1; i <= 3; i++) { for (int k = 0; k < 3; k++) { awake(w, 1); restart(w); boot(w, R_DOWN, "SWAP IN (a soft restart), no network"); }
                                   morning(w, R_DOWN, "morning, no network"); } }
static void cD(W *w, int v) { night(w, 8, true); boot(w, R_SLOW, "stopped boot, router slow (seen, no answer in 30 s)");
    for (int i = 0; i < 4; i++) morning(w, R_SLOW, "morning, router slow");
    awake(w, DAY); night(w, 8, true); boot(w, R_SLOW, "a second stop, router slow");
    for (int i = 0; i < 4; i++) morning(w, R_SLOW, "morning, router slow"); }
static void cE(W *w, int v) { night(w, 8, true); boot(w, R_UP, "stopped boot, router up");
    awake(w, DAY); night(w, 8, true); boot(w, R_UP, "another stop, router up"); morning(w, R_UP, "morning"); }
static void cE2(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down");
    for (int i = 0; i < 3; i++) morning(w, R_DOWN, "morning, router down");
    morning(w, R_UP, "morning, router back (after 4 failed tries)");
    awake(w, DAY); night(w, 8, true); boot(w, R_UP, "another stop, router up"); }
static void cF(W *w, int v) { boot(w, R_UP, "boot, router up");
    morning(w, R_DOWN, "morning (a power-off night), router down"); morning(w, R_DOWN, "morning (deep sleep), router down");
    morning(w, R_DOWN, "morning, router down"); morning(w, R_UP, "morning, router up"); morning(w, R_UP, "morning (power-off), router up");
    morning(w, R_DOWN, "morning (power-off), router down"); morning(w, R_UP, "morning (deep sleep), router up"); }
static void gfirst(W *w) { w->netclk = 0; w->netok = 0; w->chip_t = w->real - 728; w->clk = w->chip_t; w->S = w->clk; w->sys_t = w->clk; }
static void cG(W *w, int v) { gfirst(w); boot(w, R_DOWN, "boot, first sync not made, router down");
    morning(w, R_DOWN, "morning, router down"); morning(w, R_DOWN, "morning, router down"); morning(w, R_UP, "morning, router up (gave up?)");
    awake(w, DAY); night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down"); morning(w, R_UP, "morning, router up"); morning(w, R_UP, "morning"); }
static void cG2(W *w, int v) { gfirst(w); night(w, 8, true); boot(w, R_DOWN, "netclk 0, stopped boot, router down");
    morning(w, R_DOWN, "morning, router down"); morning(w, R_UP, "morning, router up"); morning(w, R_UP, "morning"); }
static void cH(W *w, int v) { awake(w, DAY); night(w, NIGHT, false); w->unread_next = true; boot(w, R_DOWN, "boot, chip did not answer, router down");
    morning(w, R_DOWN, "morning, chip fine, router down"); morning(w, R_UP, "morning, router up"); morning(w, R_UP, "morning"); }
static void cH2(W *w, int v) { awake(w, DAY); night(w, NIGHT, false); w->unread_next = true; boot(w, R_DOWN, "boot, chip did not answer, no network");
    for (int i = 0; i < 4; i++) morning(w, R_DOWN, "morning, chip fine, no network"); }
static void cH3(W *w, int v) { awake(w, DAY); night(w, NIGHT, false); w->unread_next = true; boot(w, R_UP, "boot, chip did not answer, router up");
    morning(w, R_UP, "morning"); }
static void cI1(W *w, int v) { w->saved = false; night(w, 8, true); boot(w, R_NONE, "stopped boot, no network saved: seeded");
    morning(w, R_NONE, "morning, still none saved");
    awake(w, 2); w->saved = true; w->miss = 0; restart(w); boot(w, R_UP, "the boot after a network is saved, router up");
    morning(w, R_UP, "morning, router up"); }
static void cI1p(W *w, int v) { boot(w, R_UP, "boot, router up"); w->saved = false; awake(w, DAY); night(w, NIGHT, true);
    boot(w, R_NONE, "the battery ran flat (no network saved now): seeded"); morning(w, R_NONE, "morning, none saved (deep sleep)");
    awake(w, 2); w->saved = true; w->miss = 0; restart(w); boot(w, R_UP, "the boot after a network is saved, router up");
    morning(w, R_UP, "morning, router up"); }
static void cI2(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, old network gone");
    for (int i = 0; i < 3; i++) morning(w, R_DOWN, "morning, old network gone");
    awake(w, 2); w->miss = 0; restart(w); boot(w, R_UP, "a NEW network saved, the boot after it, router up");
    morning(w, R_UP, "morning"); }
static void cI3(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, old network gone");
    morning(w, R_DOWN, "morning, old network gone");
    awake(w, 2); w->miss = 0; restart(w); boot(w, R_DOWN, "a NEW network saved, its router not on yet");
    morning(w, R_UP, "morning, the new router on"); morning(w, R_UP, "morning"); }
static void cJ(W *w, int v) { int fw = w->fw; w->netlag = ABSENT;   /* (0.4.0 wrote none) */
    w->fw = FW040; night(w, 8, true); boot(w, R_SLOW, "0.4.0: stopped boot, router slow");
    awake(w, 3); restart(w); w->fw = fw; boot(w, R_UP, v == V040 ? "CHECK FOR UPDATES, no update: router up" : "the OTA's first boot of 0.4.1, router up");
    morning(w, R_UP, "morning"); }
static void cK(W *w, int v) { for (int i = 0; i < 7; i++) morning(w, R_DOWN, "a chip with its cell, the router gone a week");
    morning(w, R_UP, "the router back"); }
static void cK2(W *w, int v) { morning(w, R_DOWN, "a chip with its cell, the router gone");
    awake(w, DAY); night(w, NIGHT, false); w->unread_next = true; boot(w, R_DOWN, "the chip did not answer, router gone");
    for (int i = 0; i < 4; i++) morning(w, R_DOWN, "morning, router gone"); morning(w, R_UP, "the router back"); }
static void cL(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down");
    morning(w, R_COLDMISS, "morning, router back, a quick look misses it"); morning(w, R_UP, "morning, router up"); }
static void cW(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down");
    for (int i = 0; i < 5; i++) morning(w, R_WEAK, "morning, router back at the edge of range"); }
static void cM(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down (owed)");
    awake(w, 3); restart(w); boot(w, R_DOWN, "the backup page's restore (the board keeps its clock keys)");
    morning(w, R_UP, "morning, router up"); }
static void cMold(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down (owed)");
    awake(w, 3); restart(w);
    if (w->netlag != ABSENT) w->netlag = 0;          /* CONTROL: a file's netlag 0 written over the board's (the old page's key list) */
    w->netgave = 0;
    boot(w, R_DOWN, "CONTROL: a restore that writes the file's netlag 0");
    morning(w, R_UP, "morning, router up"); }
static void cN(W *w, int v) { night(w, 8, true); w->cut_in_seed = true; boot(w, R_DOWN, "stopped boot, router down, a cut in the seed");
    power_back(w, 0.5); boot(w, R_DOWN, "power back, router down"); morning(w, R_UP, "morning, router up"); }
static void cN2(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down");
    w->cut_in_sync = true; morning(w, R_UP, "router up, a cut after the chip's write");
    power_back(w, 0.2); boot(w, R_UP, "power back"); morning(w, R_UP, "morning"); }

/* ---- the 0.4.1 audit's cases (docs/audit-0.4.1, R-power) ---- */
static void cP(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down (owed)");
    awake(w, 3); restart(w); w->S = w->real; w->down_at = w->real;   /* the backup page's restore: the save stamped at the COMPUTER's now (real time) */
    w->mark_away = w->away; w->mark_lived = w->lived;
    boot(w, R_DOWN, "the restore's restart (the page stamped the save at real now)");
    morning(w, R_UP, "morning, router up");
    if (w->trace) printf("    P: after the restore: away %.1f h, the restored tank lived %.1f h (the page promises nothing from before it)\n",
                         w->away - w->mark_away, w->lived - w->mark_lived); }
static void cX(W *w, int v) { night(w, 8, true); w->stamp_lost_next = true; boot(w, R_DOWN, "stopped boot, router down, a cut swap finished with no clock");
    morning(w, R_DOWN, "morning, router down"); morning(w, R_UP, "morning, router up"); }
static void cQ(W *w, int v) { awake(w, DAY); night(w, NIGHT, false); w->noprobe_next = true; boot(w, R_DOWN, "boot, the chip missed the PROBE (s_rtc false), router down");
    morning(w, R_DOWN, "morning, chip fine, router down"); morning(w, R_UP, "morning, router up"); morning(w, R_UP, "morning"); }
static void cR(W *w, int v) { night(w, 8, true); boot(w, R_DOWN, "stopped boot, router down");
    for (int i = 0; i < 2; i++) morning(w, R_DOWN, "morning, router down");
    awake(w, 1); w->miss = 0; restart(w); boot(w, R_DOWN, "CHECK FOR UPDATES connected (miss cleared), back, router gone again");
    morning(w, R_UP, "morning, router up"); }

enum { PAYS = 1, MORE = 2, CONTROL = 4, PENDANT = 8, CHEAPER = 16, RESTORE = 32 };   /* RESTORE: owed counts from the restore - the tank before it is gone */
static const struct { const char *name, *what; case_fn fn; int flags; } CASES[] = {
    { "A",   "an 8 h stop (no cell), the router still booting, up 10 min on", cA, PAYS },
    { "B",   "the same stop, the router back only at the third boot", cB, PAYS | CHEAPER },
    { "C",   "the same stop, the network gone for good", cC, CHEAPER },
    { "C2",  "C with three SWAP INs a day", cC2, CHEAPER },
    { "D",   "a router slow at EVERY boot (seen, never in 30 s), two stops", cD, 0 },
    { "E",   "a stop, a good sync, another stop", cE, PAYS },
    { "E2",  "a stop; the router back only after 4 failed tries; another stop", cE2, PAYS | CHEAPER },
    { "F",   "the pendant: power-off nights after a good try, deep sleep after a failed one", cF, PAYS | PENDANT },
    { "G",   "the first sync never made (netclk 0, an old build-time clock)", cG, 0 },
    { "G2",  "netclk 0, a stop, the router down then up", cG2, 0 },
    { "H",   "a chip that ran, its read failed at one boot, router down", cH, PAYS | CHEAPER },
    { "H2",  "H with the router down for good", cH2, PAYS | CHEAPER },
    { "H3",  "H with the router up", cH3, PAYS },
    { "I1",  "a seed while NO network was saved, then a network saved (netok 1 from before)", cI1, PAYS },
    { "I1p", "I1 on the pendant (the battery ran flat): paid at the setup boot, 0.4.0 the next morning", cI1p, PAYS | PENDANT | MORE },
    { "I2",  "a stop, the old network gone 4 boots, a NEW network saved, up", cI2, PAYS | CHEAPER },
    { "I3",  "a stop, the old network gone, a NEW one saved before its router is on", cI3, PAYS },
    { "J",   "0.4.0 owes a stretch (stopped boot, router slow), the OTA to 0.4.1 that day", cJ, PAYS },
    { "K",   "a chip with its cell, the router gone a week (nothing owed)", cK, PAYS },
    { "K2",  "K with a failed chip read in the week", cK2, PAYS | CHEAPER },
    { "L",   "a stop; the router back, but a quick look's sweeps miss it once", cL, PAYS | MORE },
    { "W",   "a stop; the router back at the edge of range: only the full look finds it", cW, PAYS | MORE },
    { "M",   "an owed board restored from a backup made before (the page keeps its clock keys)", cM, PAYS },
    { "M0",  "CONTROL: the same restore writing the file's netlag 0 (why the page keeps the board's)", cMold, CONTROL },
    { "N",   "a power cut in the seed (after netlag 1, before the chip's write)", cN, PAYS },
    { "N2",  "a power cut in the good sync (after the chip's write, before netlag 0)", cN2, PAYS | MORE },
    { "P",   "an owed board restored, the page stamping the save at the computer's now", cP, PAYS | RESTORE },
    { "X",   "a cut swap finished at a boot with no clock (stamp 0): the seed at the BUILD time", cX, PAYS },
    { "Q",   "the chip missed the probe at one boot (clockless for that boot), router down", cQ, PAYS },
    { "R",   "owed, CHECK FOR UPDATES connects (miss cleared), the router gone again, then back", cR, PAYS | MORE },
};

/* every input of the gate: 0.4.1 parts from 0.4.0 only where it should */
static int gate_sweep(void) {
    int fails = 0, skips = 0, asks = 0;
    for (int rtc = 0; rtc < 2; rtc++) for (int real = 0; real < 2; real++) for (int set = 0; set < 2; set++) for (int ok = 0; ok < 2; ok++)
    for (int lag = -1; lag <= 1; lag++) for (int gave = 0; gave <= 4; gave++) for (int miss = 0; miss <= 8; miss++) {
        const clock_gate_in_t g = { rtc, real, set, ok, lag, gave, miss };
        const bool a1 = clock_gate_asks(clock_gate(&g)), a0 = clock_gate_asks(clock_gate_040(&g));
        if (!a1 && a0) { skips++; if (!(rtc && real && set && lag == CLOCK_LAG_NONE)) { fails++; printf("  FAIL gate: skips rtc %d real %d set %d ok %d lag %d gave %d miss %d (0.4.0 asked)\n", rtc, real, set, ok, lag, gave, miss); } }
        if (a1 && !a0) { asks++; if (lag != CLOCK_LAG_OWED) { fails++; printf("  FAIL gate: asks rtc %d real %d set %d ok %d lag %d (0.4.0 skipped)\n", rtc, real, set, ok, lag); } }
        if (lag == CLOCK_LAG_OWED && real && !a1) { fails++; printf("  FAIL gate: owed on real time and not asked (rtc %d set %d ok %d)\n", rtc, set, ok); }
        if (clock_gate(&g) == CLOCK_ASK_QUICK && !(rtc && miss > 0)) { fails++; printf("  FAIL gate: a quick look off a chip-board or without a miss\n"); }
        if (clock_gate(&g) == CLOCK_ASK_QUICK && miss % CLOCK_FULL_EVERY == 0) { fails++; printf("  FAIL gate: a quick look on the full look's turn (miss %d)\n", miss); }
    }
    printf("selftest-clock-gate: the gate over all %d inputs: 0.4.1 skips where 0.4.0 asks in %d (each: on real time, nothing owed), asks where it skipped in %d (each: owed)\n",
           2 * 2 * 2 * 2 * 3 * 5 * 9, skips, asks);
    return fails;
}

int selftest_clock_gate(void) {
    const bool trace = getenv("PT_CLOCK_TRACE") && *getenv("PT_CLOCK_TRACE") == '1';
    int fails = gate_sweep();
    const int n = (int)(sizeof CASES / sizeof *CASES);
    printf("selftest-clock-gate: %d cases x {0.4.0, 0.4.1 off the OTA, 0.4.1 settled}: pages (quick looks) / TIME CHECK s / owed h at the end\n", n);
    double tot[NV] = { 0 };
    for (int c = 0; c < n; c++) {
        double owed[NV], secs[NV]; int pages[NV], quick[NV], moved[NV];
        if (trace) printf("\n%s. %s\n", CASES[c].name, CASES[c].what);
        for (int v = 0; v < NV; v++) {
            W w; init(&w, v == V040 ? FW040 : FW041, !(CASES[c].flags & PENDANT), trace);
            if (v == V041_SET) w.netlag = 0;
            if (trace) printf("  %s\n", VNAME[v]);
            CASES[c].fn(&w, v);
            owed[v] = CASES[c].flags & RESTORE ? (w.away - w.mark_away) - (w.lived - w.mark_lived) : w.away - w.lived; secs[v] = w.secs;
            moved[v] = w.moved; pages[v] = w.pages; quick[v] = w.quick; tot[v] += w.secs;
        }
        char line[256]; int at = snprintf(line, sizeof line, "  %-4s", CASES[c].name);
        for (int v = 0; v < NV; v++) at += snprintf(line + at, sizeof line - at, " | %2d (%d) %4.0f s %5.1f h", pages[v], quick[v], secs[v], owed[v]);
        const char *bad = NULL;
        for (int v = V041_OTA; v < NV && !bad; v++) {
            if (CASES[c].flags & CONTROL) { if (v == V041_SET && owed[v] < 1) bad = "the control lost nothing: the model does not see the hole"; continue; }
            if (moved[v] && !(CASES[c].flags & RESTORE)) bad = "the cold boot moved the clock up outside a restore (it could drop a payment)";
            else if (owed[v] < -1e-6) bad = "lived MORE than the time away (a jump paid twice)";
            else if (owed[v] > (owed[V040] > 0 ? owed[V040] : 0) + 1e-6) bad = "owes more than 0.4.0";   /* (0.4.0 overpaying is not owing less: X) */
            else if ((CASES[c].flags & PAYS) && owed[v] > 1e-6) bad = "time still owed in a case the network pays";
            else if (!(CASES[c].flags & MORE) && secs[v] > secs[V040] + 1e-6 && !(owed[V040] > owed[v] + 1e-6))
                bad = "a longer TIME CHECK than 0.4.0";      /* (allowed where it is the ask that pays what 0.4.0 lost: I1) */
            else if ((CASES[c].flags & CHEAPER) && v == V041_SET && secs[v] >= secs[V040]) bad = "no cheaper than 0.4.0";
        }
        printf("%s%s%s\n", line, bad ? "  FAIL: " : "", bad ? bad : "");
        if (bad) fails++;
        if (strcmp(CASES[c].name, "I1") == 0 && owed[V040] < 1) { printf("  FAIL: I1 no longer skips under 0.4.0 - the model drifted from 0.4.0's\n"); fails++; }
    }
    {   /* the yardstick sees a hole: cbd63a8's skip left B's, I2's and L's stretch owed for good */
        static const case_fn HOLE[] = { cB, cI2, cL };
        for (int k = 0; k < 3; k++) {
            W w; init(&w, FWCBD, true, false); HOLE[k](&w, V040);
            if (w.away - w.lived < 1) { printf("  FAIL: cbd63a8's skip lost nothing in case %d - the model is blind to it\n", k); fails++; }
        }
        printf("selftest-clock-gate: control: cbd63a8's skip (reverted in 0.4) leaves B, I2 and L owing for good in the model\n");
    }
    printf("selftest-clock-gate: TIME CHECK in all: 0.4.0 %.0f s, 0.4.1 off the OTA %.0f s, 0.4.1 settled %.0f s\n", tot[V040], tot[V041_OTA], tot[V041_SET]);
    printf("selftest-clock-gate: %s\n", fails ? "FAILED" : "OK - never more owed than 0.4.0, nothing owed where the network pays, the time check cheaper");
    return fails ? 1 : 0;
}
