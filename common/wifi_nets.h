/* wifi_nets.h - THE SAVED NETWORKS (0.4.2): the tank keeps up to three Wi-Fi networks and joins whichever it can
 * see (Strato: "up to three wifi networks and it tries them one at a time until it finds an SSID match"). Pure, no
 * platform: the list, what a new one replaces, which one a scan picks, the move from 0.4.1's one network. The
 * boards (firmware/main/net_port_esp.c) and the desktop (sim/net_port_sim.c) only read and write the bytes, so
 * --selftest-wifi3 drives the very lines the boards run.
 *
 * THE LIST IS KEPT IN THE ORDER THE TANK TRIES THEM: net[0] is the one used last (joined, or just saved by the
 * keeper), net[n - 1] the one used longest ago. So:
 *  - the fast path is net[0], on its learned channel - one saved network is exactly 0.4.1;
 *  - a scan picks the FIRST of the list that the radio sees (the most recently used of those in range: never the
 *    strongest signal - the order is the tank's own history, so there are no ties); one that is in range and will
 *    not join is passed over for the next (wifi_nets_next). What a scan saw and what was tried are kept BY NAME
 *    (wifi_names_t): the list reorders when one joins, and a place in it would then mean another network;
 *  - a fourth network replaces net[2], the one used longest ago;
 *  - joining one moves it to the front; forgetting one closes the gap and keeps the others' order.
 * `chan` (the channel a network last answered on, 0 = not known) is each network's own. The quick look's "miss"
 * count (clock_gate.h) stays ONE number beside the list: it counts boots that saw NONE of the saved networks.
 *
 * THE BYTES (the boards' NVS blob "nets", the sim's <wifi file>.nets): this struct as it is, 299 bytes, every
 * field a byte (no padding, no byte order). One write replaces the whole list - a power cut leaves the old list or
 * the new one, never half of each. Passwords live only here and in the radio's hands: never in a log.
 *
 * 0.4.1'S KEYS (ssid / pass / chan, one network), THE RULE: whenever they are found they are ADDED to the list
 * (wifi_nets_take_old: a new name goes in first, a name already there takes the old keys' password), the list is
 * written, and THEN the old keys are erased. So the list and the old keys can never disagree for longer than one
 * read; a cut between the two steps repeats the same harmless add. A tank rolled back to 0.4.1 by hand finds NO
 * network (downgrade is not a keeper's path); one it is given there comes into the list on the way back up; and a
 * network forgotten in 0.4.2 cannot come back from the old keys, because they are gone before FORGET can be tapped.
 * The boards keep the old keys a little longer in ONE case (firmware/main/net_old_keys.h, 0.4.2 audit r2 PW2-1):
 * while an update still waits for its confirmation, because a reset then rolls back to 0.4.1 on its own. A SAVE or
 * a FORGET erases them even then, so the last sentence stands. */
#ifndef POCKET_TANK_WIFI_NETS_H
#define POCKET_TANK_WIFI_NETS_H
#include <stddef.h>
#include "update.h"

#define WIFI_NETS_MAX  NET_SAVED_MAX
#define WIFI_NETS_VER  1
typedef struct {
    char    ssid[NET_SSID_MAX + 1];
    char    pass[NET_PASS_MAX + 1];
    uint8_t chan;                    /* the channel it last answered on (1..14), 0 = not known */
    uint8_t joined;                  /* 1 = the tank has been on it (the UPDATES page's LAST USED is never said of one only typed in) */
} wifi_net_t;
typedef struct { uint8_t ver, n; wifi_net_t net[WIFI_NETS_MAX]; } wifi_nets_t;
_Static_assert(sizeof(wifi_net_t) == 99 && sizeof(wifi_nets_t) == 299, "the saved networks' bytes are the boards' NVS blob: every field a byte");

void wifi_nets_clear(wifi_nets_t *w);
/* bytes read back from the store: false (and *w empty) unless they are a whole, sane list (bytes may be w itself) */
bool wifi_nets_from_bytes(wifi_nets_t *w, const void *bytes, size_t len);
/* 0.4.1's one network (NVS ssid / pass / chan) into the list, first: it had been joined if its channel was
 * learned. A name already in the list takes this password and keeps its place in the count. false = no name */
bool wifi_nets_take_old(wifi_nets_t *w, const char *ssid, const char *pass, int chan);
int  wifi_nets_find(const wifi_nets_t *w, const char *ssid);                 /* its place in the list, -1 = not saved */
/* saving `ssid` now would push one out (three saved, this name not among them): its place, else -1 */
int  wifi_nets_victim(const wifi_nets_t *w, const char *ssid);
/* save one: a name already saved keeps its place in the count and takes the new password; a new one goes in, and
 * with three saved the one used longest ago goes (its name into replaced, NET_SSID_MAX + 1, "" if none; may be NULL).
 * Either way it is tried first from now on. false = no name */
bool wifi_nets_add(wifi_nets_t *w, const char *ssid, const char *pass, char *replaced);
/* the tank is on `ssid` (chan 0 = not known: the old one stands): to the front. true = the list changed (write it) */
bool wifi_nets_joined(wifi_nets_t *w, const char *ssid, int chan);
bool wifi_nets_forget_one(wifi_nets_t *w, const char *ssid);                 /* false = not saved */
/* a few network names: the saved ones a scan heard, the ones tried since. By name, never by place in the list */
typedef struct { uint8_t n; char ssid[WIFI_NETS_MAX][NET_SSID_MAX + 1]; } wifi_names_t;
void wifi_names_clear(wifi_names_t *s);
bool wifi_names_has(const wifi_names_t *s, const char *ssid);
void wifi_names_add(wifi_names_t *s, const char *ssid);                      /* (one already there, or a fourth: nothing) */
/* what a scan heard, one name at a time: kept in `seen` if it is a saved one. Its place in `seen` (the port keeps
 * the channel beside it), -1 = not saved, or there already */
int  wifi_nets_heard(const wifi_nets_t *w, wifi_names_t *seen, const char *name);
/* THE ONE TO JOIN NEXT: the first of the list, as it stands NOW, that the scan heard and that has not been tried
 * since - its place, -1 = none left. wifi_nets_left: how many such there are (the time check shares what is left
 * of its budget between them) */
int  wifi_nets_next(const wifi_nets_t *w, const wifi_names_t *seen, const wifi_names_t *tried);
int  wifi_nets_left(const wifi_nets_t *w, const wifi_names_t *seen, const wifi_names_t *tried);
/* a name as a person typed it on a command line (len characters of s): the spaces before and after it go, the
 * ones inside stay (a real name may hold them), NET_SSID_MAX kept. out: NET_SSID_MAX + 1 */
void wifi_name_arg(char *out, const char *s, int len);
/* a name for a row `max` characters wide: as it is, or its head, "..." and its tail (two long names that differ
 * at the end - HOME-2G / HOME-5G - stay two names). max >= 8 */
void wifi_name_fit(char *out, size_t cap, const char *ssid, int max);

/* the platform's one: the list as the store holds it now, the port's own copy (never NULL; n 0 = none saved or a
 * store that cannot be read; good until the port's next call). No copy on the caller's stack: the boards ask from
 * small tasks. A tank from 0.4.1 hands its one network over inside this call (see the ports). update.h's
 * net_port_creds_get / _count / _at / _replaces are wifi_nets.c's, over it; _set / _forget_one / _forget are a
 * few lines of each port's own (they write). The pointer is to a list that is never changed in place: a change
 * makes a new one (the boards: net_port_esp.c nets_edit), so a reader on another task never sees half a move */
const wifi_nets_t *net_port_nets(void);
/* after a scan's NET_DONE: the saved network to join next, of everything the scan heard (not just the strongest
 * NET_SCAN_MAX) - wifi_nets_next against the list as it is now and `tried`. Its place, name and password; -1 = none */
int  net_port_scan_saved(const wifi_names_t *tried, char *ssid, char *pass);
#endif
