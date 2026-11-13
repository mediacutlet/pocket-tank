/* net_old_keys.h - 0.4.1's Wi-Fi keys (NVS ssid / pass / chan): are they erased at THIS write of the list? (0.4.2)
 *
 * Pure: no ESP-IDF, so the host can test it (sim/selftest_old_keys.c, --selftest-old-keys in make check).
 * net_port_esp.c's nets_flash writes the list ("nets") and then asks here; nothing else decides it.
 *
 * WHY (0.4.2 audit round 2, PW2-1): the first boot after an update from 0.4.1 moves 0.4.1's network into the list
 * at the boot's time check - long before the new version is confirmed (main.c, ota_valid_now). Any reset before
 * that (a crash, the watchdog, a brownout, the cable pulled) sends the tank back to 0.4.1, and 0.4.1 reads only
 * the old keys: erased, it had no network. So while the running image is still PENDING its confirmation the tank's
 * OWN writes (the move itself, a join's move to the front) leave the old keys be; they go at the next write once
 * the image is confirmed - at the latest the next boot's read, which finds them, adds them again (the same
 * harmless add) and writes.
 *
 * A KEEPER'S change always erases them, pending or not: a network forgotten or replaced must never come back from
 * the old keys (wifi_nets.h, 0.4.1'S KEYS). FORGET ALL erases them itself, before the list (net_port_creds_forget). */
#ifndef NET_OLD_KEYS_H
#define NET_OLD_KEYS_H
#include <stdbool.h>

typedef enum {
    NET_KEYS_MOVED,     /* the tank's own: the boot's read found 0.4.1's keys and put them in the list */
    NET_KEYS_JOINED,    /* the tank's own: the radio joined one, it goes to the front / its channel is learned */
    NET_KEYS_SAVED,     /* the keeper's: a network saved (it may push another out) */
    NET_KEYS_FORGOT,    /* the keeper's: one network forgotten */
} net_keys_why_t;

/* pending: the running image still waits for its confirmation (ESP_OTA_IMG_PENDING_VERIFY: only ever after an
 * update over the air; a USB install is never pending) */
static inline bool net_old_keys_erase_now(net_keys_why_t why, bool pending) {
    if (why == NET_KEYS_SAVED || why == NET_KEYS_FORGOT) return true;
    return !pending;
}
#endif
