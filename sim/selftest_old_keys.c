/* selftest_old_keys.c - --selftest-old-keys (0.4.2 audit round 2, PW2-1): 0.4.1's Wi-Fi keys across an update
 * that has not been confirmed yet.
 *
 * firmware/main/net_old_keys.h is the decision net_port_esp.c's nets_flash makes ("erase the old keys at this
 * write?"); it is pure, so it is tested here. The firmware's NVS can't run on the host: the store below is a
 * MODEL of it (the blob "nets", the keys ssid / pass / chan) and m_load / m_edit / m_forget_all repeat
 * net_port_esp.c's nets_load / nets_edit / net_port_creds_forget step for step, with the real list code
 * (common/wifi_nets.c) and the real decision. What it pins:
 *  A. a USB install (never pending): the old keys are erased at the move, as before the fix;
 *  B. the first boot after an update, still pending: the list is written and the old keys STAY (the boot's read
 *     and a join's write), so a reset that rolls back finds 0.4.1's network, password and channel;
 *  C. once confirmed they go at the next write: a join in the same boot, or the next boot's read (one network in
 *     the list, not two);
 *  D. a keeper's FORGET, FORGET ALL or a SAVE that pushes the old one out erases them even while pending: nothing
 *     forgotten comes back at the next boot;
 *  E. a tank rolled back, used on 0.4.1 and updated again (a list AND old keys): one entry per name, 0.4.1's
 *     password (the newest) kept, the others' order kept; a new name goes first and pushes the last out;
 *  F. the decision's whole table; not pending = erase, whatever the reason (exactly the code before the fix).
 * PT_OLD_KEYS_OLD=1: the control - always erase, as 0.4.2 did before the fix: B fails and shows the test sees it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_nets.h"
#include "../firmware/main/net_old_keys.h"

static int fail;
#define KF(...) do { fail++; printf("selftest-old-keys: FAIL "); printf(__VA_ARGS__); printf("\n"); } while (0)

static bool erase_now(net_keys_why_t why, bool pending) {
    if (getenv("PT_OLD_KEYS_OLD")) return true;
    return net_old_keys_erase_now(why, pending);
}

/* the model of the NVS namespace "wifi" */
static struct { bool has_nets; wifi_nets_t nets; bool has_old; char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1]; int chan; } F;
static wifi_nets_t L;                                       /* the list in RAM (the firmware's s_nb[s_cur]) */
static bool s_pending;                                      /* the running image's state */

static void flash_clear(void) { memset(&F, 0, sizeof F); wifi_nets_clear(&L); s_pending = false; }
static void old_put(const char *ssid, const char *pass, int chan) {   /* what 0.4.1 saved */
    F.has_old = true; snprintf(F.ssid, sizeof F.ssid, "%s", ssid); snprintf(F.pass, sizeof F.pass, "%s", pass); F.chan = chan;
}
static void m_flash(const wifi_nets_t *w, bool keys_out) {  /* nets_flash: the list, then (if asked) the old keys out */
    F.nets = *w; F.has_nets = true;
    if (keys_out) { F.has_old = false; F.ssid[0] = F.pass[0] = 0; F.chan = 0; }
}
static void m_load(void) {                                  /* nets_load: a boot's first read */
    wifi_nets_t w;
    if (!F.has_nets || !wifi_nets_from_bytes(&w, &F.nets, sizeof F.nets)) wifi_nets_clear(&w);
    bool old = F.has_old && F.ssid[0];
    if (old) old = wifi_nets_take_old(&w, F.ssid, F.pass, F.chan);
    L = w;
    if (old) m_flash(&w, erase_now(NET_KEYS_MOVED, s_pending));
}
enum { ED_ADD, ED_JOINED, ED_FORGET };
static bool m_edit(int op, const char *ssid, const char *pass, int chan) {   /* nets_edit */
    wifi_nets_t w = L;
    bool ch = op == ED_ADD ? wifi_nets_add(&w, ssid, pass, NULL) : op == ED_JOINED ? wifi_nets_joined(&w, ssid, chan) : wifi_nets_forget_one(&w, ssid);
    const net_keys_why_t why = op == ED_ADD ? NET_KEYS_SAVED : op == ED_JOINED ? NET_KEYS_JOINED : NET_KEYS_FORGOT;
    if (ch) { L = w; m_flash(&w, erase_now(why, s_pending)); }
    return ch;
}
static void m_forget_all(void) {                            /* net_port_creds_forget: the old keys first, then the list */
    F.has_old = false; F.ssid[0] = F.pass[0] = 0; F.chan = 0;
    F.has_nets = false; memset(&F.nets, 0, sizeof F.nets);
    m_load();
}
static void boot(bool pending) { s_pending = pending; wifi_nets_clear(&L); m_load(); }
/* what 0.4.1 would join after a rollback: it reads only the old keys */
static bool v041_has(const char *ssid, const char *pass, int chan) { return F.has_old && !strcmp(F.ssid, ssid) && !strcmp(F.pass, pass) && F.chan == chan; }
static int count_of(const wifi_nets_t *w, const char *ssid) { int c = 0; for (int i = 0; i < w->n; i++) if (!strcmp(w->net[i].ssid, ssid)) c++; return c; }
static bool at(const wifi_nets_t *w, int i, const char *ssid, const char *pass) { return i < w->n && !strcmp(w->net[i].ssid, ssid) && (!pass || !strcmp(w->net[i].pass, pass)); }

int selftest_old_keys(void) {
    /* A. a USB install: never pending */
    flash_clear(); old_put("Home", "home-pass", 6); boot(false);
    if (L.n != 1 || !at(&L, 0, "Home", "home-pass") || !F.has_nets || F.nets.n != 1) KF("A: a USB install did not move 0.4.1's network into the list (%d in the list)", L.n);
    if (F.has_old) KF("A: a USB install left 0.4.1's keys on the flash (they went at once before the fix)");
    printf("selftest-old-keys: A  a USB install (not pending): the list holds %d, 0.4.1's keys %s\n", L.n, F.has_old ? "STAY" : "erased");

    /* B. the first boot after an update: pending */
    flash_clear(); old_put("Home", "home-pass", 6); boot(true);
    if (L.n != 1 || !at(&L, 0, "Home", "home-pass") || !F.has_nets || F.nets.n != 1 || F.nets.net[0].chan != 6) KF("B: the pending boot did not write the list (0.4.2 itself must lose nothing)");
    if (!v041_has("Home", "home-pass", 6)) KF("B: the boot's read erased 0.4.1's keys while the update is not confirmed: a reset now rolls back to a 0.4.1 with NO network");
    printf("selftest-old-keys: B  the first boot after an update (pending): the list holds %d, 0.4.1's keys %s\n", L.n, F.has_old ? "stay" : "ERASED");
    if (!m_edit(ED_JOINED, "Home", NULL, 11)) KF("B: the join on another channel changed nothing");
    if (F.nets.net[0].chan != 11) KF("B: the join's channel was not written");
    if (!v041_has("Home", "home-pass", 6)) KF("B: the time check's join erased 0.4.1's keys while the update is not confirmed");
    printf("selftest-old-keys: B  ... and after the time check joined it: 0.4.1's keys %s\n", F.has_old ? "stay" : "ERASED");

    /* C. confirmed: gone at the next write */
    s_pending = false;                                      /* (the same boot, 50 frames later) */
    if (!m_edit(ED_JOINED, "Home", NULL, 1) || F.has_old) KF("C: confirmed, a join's write left 0.4.1's keys");
    flash_clear(); old_put("Home", "home-pass", 6); boot(true);   /* no write after the confirmation: the next boot's read does it */
    boot(false);
    if (F.has_old) KF("C: the boot after the confirmed one left 0.4.1's keys");
    if (L.n != 1 || count_of(&L, "Home") != 1 || !at(&L, 0, "Home", "home-pass") || F.nets.n != 1) KF("C: the second read of the same keys made %d entries (%d of Home)", L.n, count_of(&L, "Home"));
    boot(false);
    if (L.n != 1 || F.has_old) KF("C: a third boot changed the list (%d)", L.n);
    printf("selftest-old-keys: C  confirmed: the next write erases them (a join, or the next boot's read): the list holds %d, keys %s\n", L.n, F.has_old ? "STAY" : "erased");

    /* D. a keeper's change erases them, pending or not */
    for (int pend = 0; pend < 2; pend++) {
        flash_clear(); old_put("Home", "home-pass", 6); boot(pend);
        if (!m_edit(ED_FORGET, "Home", NULL, 0) || F.has_old) KF("D: FORGET %s left 0.4.1's keys", pend ? "while pending" : "(not pending)");
        boot(false);
        if (L.n != 0) KF("D: a network forgotten %s CAME BACK at the next boot (%d in the list)", pend ? "while pending" : "(not pending)", L.n);
        boot(true);
        if (L.n != 0) KF("D: a network forgotten %s came back at a later pending boot", pend ? "while pending" : "(not pending)");

        flash_clear(); old_put("Home", "home-pass", 6); boot(pend);
        m_forget_all();
        if (F.has_old || F.has_nets || L.n != 0) KF("D: FORGET ALL %s left something (keys %d, list %d)", pend ? "while pending" : "(not pending)", F.has_old, L.n);
        boot(false);
        if (L.n != 0) KF("D: FORGET ALL %s: a network came back", pend ? "while pending" : "(not pending)");

        /* the old one is the one used longest ago; a fourth network pushes it out */
        flash_clear(); old_put("Home", "home-pass", 6); boot(pend);
        m_edit(ED_ADD, "A", "a-pass-11", -1); m_edit(ED_ADD, "B", "b-pass-11", -1);
        if (pend && F.has_old) KF("D: a SAVE while pending left 0.4.1's keys");
        old_put("Home", "home-pass", 6);                    /* (as if they had stayed until now) */
        m_edit(ED_ADD, "C", "c-pass-11", -1);
        if (F.has_old) KF("D: the SAVE that pushed 0.4.1's network out %s left its keys", pend ? "while pending" : "(not pending)");
        boot(false);
        if (count_of(&L, "Home") != 0 || L.n != 3 || !at(&L, 0, "C", NULL)) KF("D: the network a SAVE replaced %s came back (%d of Home, first %s)", pend ? "while pending" : "(not pending)", count_of(&L, "Home"), L.net[0].ssid);
    }
    printf("selftest-old-keys: D  FORGET, FORGET ALL and a SAVE that replaces it erase 0.4.1's keys, pending or not: nothing comes back\n");

    /* E. rolled back, used on 0.4.1, updated again: a list AND old keys */
    for (int pend = 0; pend < 2; pend++) {
        flash_clear(); m_edit(ED_ADD, "B", "b-pass-11", -1); m_edit(ED_ADD, "Home", "home-OLD", -1); m_edit(ED_ADD, "A", "a-pass-11", -1);   /* A, Home, B */
        old_put("Home", "home-NEW", 6);                     /* 0.4.1 was given the new password */
        boot(pend);
        if (L.n != 3 || count_of(&L, "Home") != 1 || !at(&L, 0, "Home", "home-NEW") || !at(&L, 1, "A", "a-pass-11") || !at(&L, 2, "B", "b-pass-11"))
            KF("E: the same name in both (%s): %d in the list, %d of Home, first %s", pend ? "pending" : "not pending", L.n, count_of(&L, "Home"), L.net[0].ssid);
        if (F.has_old != (pend == 1)) KF("E: the same name in both (%s): 0.4.1's keys %s", pend ? "pending" : "not pending", F.has_old ? "stay" : "erased");
        wifi_nets_t first = L;
        boot(false);
        if (memcmp(&first, &L, sizeof L) || F.has_old || count_of(&L, "Home") != 1) KF("E: the boot after changed the merged list, or left the keys");

        flash_clear(); m_edit(ED_ADD, "C", "c-pass-11", -1); m_edit(ED_ADD, "B", "b-pass-11", -1); m_edit(ED_ADD, "A", "a-pass-11", -1);     /* A, B, C */
        old_put("Cabin", "cabin-pw", 0);                    /* 0.4.1 was given another network */
        boot(pend);
        if (L.n != 3 || !at(&L, 0, "Cabin", "cabin-pw") || !at(&L, 1, "A", NULL) || !at(&L, 2, "B", NULL) || count_of(&L, "C") != 0)
            KF("E: a new name over a full list (%s): %d in the list, first %s", pend ? "pending" : "not pending", L.n, L.net[0].ssid);
        first = L;
        boot(false);
        if (memcmp(&first, &L, sizeof L) || F.has_old || count_of(&L, "Cabin") != 1) KF("E: the boot after changed the list a new name went into, or left the keys");
    }
    printf("selftest-old-keys: E  a list and 0.4.1's keys both there: one entry a name, 0.4.1's password kept, first in the list, the same at the next boot\n");

    /* F. the decision's table */
    static const struct { net_keys_why_t why; const char *name; bool pending_erases; } T[] = {
        { NET_KEYS_MOVED,  "the boot's move",   false },
        { NET_KEYS_JOINED, "a join",            false },
        { NET_KEYS_SAVED,  "a keeper's SAVE",   true  },
        { NET_KEYS_FORGOT, "a keeper's FORGET", true  },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        if (!net_old_keys_erase_now(T[i].why, false)) KF("F: %s, not pending, does not erase (it did before the fix)", T[i].name);
        if (erase_now(T[i].why, true) != T[i].pending_erases) KF("F: %s while pending: erase %d, wanted %d", T[i].name, !T[i].pending_erases, T[i].pending_erases);
    }
    printf("selftest-old-keys: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
