/* now_relay.h — the home tank's radio is a SECOND CHIP (2026-10-13).
 *
 * The ESP32-P4 has no radio. On the Waveshare P4 10.1 an ESP32-C6 sits beside
 * it on SDIO, driven through Espressif's hosted stack (esp_hosted), which
 * gives the P4 Wi-Fi and Bluetooth - but not ESP-NOW, the link the pocket
 * tanks speak (docs/board-p4-10.1.md, "The radio"). So the C6 runs OUR
 * firmware (firmware-c6/): the stock hosted coprocessor plus a small relay.
 * ESP-NOW frames cross between the two chips as hosted "peer data" messages
 * (esp_hosted 3.x: application messages keyed by a 32-bit id), in the layout
 * below. The pocket tanks need nothing: on the air it is plain ESP-NOW.
 *
 * Who owns what: the P4 (the host) brings Wi-Fi up through the hosted Wi-Fi
 * calls as it would its own radio - init, station mode, start - and then
 * sends NR_MSG_UP; the C6 starts ESP-NOW on that radio and answers with
 * NR_MSG_STATE. Every frame the C6 hears goes up as NR_MSG_RECV; every
 * NR_MSG_SEND goes out (to the broadcast address or to one tank).
 *
 * This file is shared by both firmwares: bytes only, no pointers, packed.
 *
 * VERSION 2 (0.4.3, the 11-24 radio audit; the image keepers get through the one install):
 *  - every request carries a number and the C6 echoes it in its NR_MSG_STATE: an answer that comes late (after its
 *    request timed out) is no longer taken as the next request's (nr_state_t.req);
 *  - the C6 says whether its radio has really sent since ESP-NOW came up (nr_state_t.radio_ok): its guard confirms a
 *    new image on that, not on any answer.
 * Old and new speak to each other: the host PINGs first (a v1 relay ignores the body and answers in v1's shorter
 * NR_MSG_STATE, so the host learns the relay's version) and then speaks the relay's version; a v2 relay takes v1
 * requests (no number: req 0).
 *
 * VERSION 3 (0.4.3, the one-install audit's M2 and M3): the relay marks itself valid for a bootloader with app
 * rollback on (firmware-c6/main/guard.c), and every answer reports the C6's app slots (nr_state_t.slots, run_sub,
 * next_sub, next_kb): where it runs, where an update would go, and whether its guard could turn such an update back to
 * the firmware running now. The host refuses to write a C6 that says it could not. The wire is v2's plus 4 bytes at
 * the end: a v2 host reads the first 32 and ignores the rest, a v1 host the first 28; a v2 relay's 32-byte answer to a
 * v3 host leaves the report 0 ("not reported"). Requests are v2's. */
#pragma once
#include <stdint.h>

#define NR_VERSION      3
#define NR_VERSION_MIN  1                   /* the oldest relay the host still speaks to (Strato's two C6s of 10-20 run v1) */
#define NR_DATA_MAX     250                 /* an ESP-NOW frame's payload */

/* host -> C6 */
#define NR_MSG_UP       0x50540001u         /* nr_up_t: start ESP-NOW on this channel */
#define NR_MSG_DOWN     0x50540002u         /* (empty): stop ESP-NOW */
#define NR_MSG_CHAN     0x50540003u         /* nr_up_t: move to this channel (every tank in a row shares it) */
#define NR_MSG_SEND     0x50540004u         /* nr_frame_t + the payload */
#define NR_MSG_PING     0x50540005u         /* (empty): answer with NR_MSG_STATE */
/* C6 -> host */
#define NR_MSG_STATE    0x50540081u         /* nr_state_t: after UP / DOWN / CHAN / PING */
#define NR_MSG_RECV     0x50540082u         /* nr_frame_t + the payload: a frame from the air */

typedef struct __attribute__((packed)) {
    uint8_t  version;                       /* the version the host speaks (a v1 relay refuses anything but 1) */
    uint8_t  channel;                       /* 1..13 */
    uint16_t req;                           /* (v2) the request's number, echoed in the answer; a v1 host sends 2 bytes */
} nr_up_t;
#define NR_UP_V1_LEN 2                      /* nr_up_t as v1 had it: version + channel */
typedef struct __attribute__((packed)) {    /* (v2) the body of PING / DOWN: a v1 relay ignores it */
    uint8_t  version;
    uint8_t  pad;
    uint16_t req;
} nr_ask_t;

typedef struct __attribute__((packed)) {
    uint8_t  mac[6];                        /* SEND: where to (ff x 6 = everyone); RECV: who from */
    int8_t   rssi;                          /* RECV: dBm (0 on SEND) */
    uint8_t  pad;
} nr_frame_t;                               /* ... then 1..NR_DATA_MAX bytes */

typedef struct __attribute__((packed)) {
    uint8_t  version;                       /* the C6 firmware's NR_VERSION */
    uint8_t  up;                            /* ESP-NOW is running */
    uint8_t  channel;                       /* the radio's channel now */
    int8_t   err;                           /* 0, or why the last request failed (NR_ERR_*) */
    uint8_t  mac[6];                        /* the C6's station address: the home tank's address on the air */
    uint16_t guard_s;                       /* 0: this C6 image is confirmed; else it is on probation (firmware-c6: THE GUARD): v1, the seconds
                                               left (this very answer confirmed it); v2, the seconds left in this boot's count, or 0xFFFF when
                                               this boot is counted already - v2 confirms only when its radio has sent (radio_ok) */
    uint32_t rx, tx, tx_fail, dropped;      /* frames heard / sent / not acked / dropped (the queue to the host was full) */
    /* (v2 from here: a v1 relay's answer ends above, NR_STATE_V1_LEN) */
    uint16_t req;                           /* the number of the request this answers (0: a v1 request, or none) */
    uint8_t  radio_ok;                      /* ESP-NOW is up and a frame of ours has gone out on the air since */
    uint8_t  slots;                         /* (v3; v2 sent 0 here) NR_SLOT_*: the C6's app slots */
    /* (v3 from here: a v2 relay's answer ends above, NR_STATE_V2_LEN) */
    uint8_t  run_sub;                       /* the running app's partition subtype: 0x00 factory, 0x10 + n = ota_n */
    uint8_t  next_sub;                      /* the slot an update would go into (its subtype; 0xff: none) */
    uint16_t next_kb;                       /* ... its size, KB */
    uint8_t  elf8[8];                       /* (v3, 0.4.3 review 7) the running image's app_elf_sha256, first 8 bytes - which build it is,
                                               not just which version (an earlier v3 build sends 36 bytes: zeros here) */
} nr_state_t;
#define NR_STATE_V1_LEN 28                  /* sizeof nr_state_t in v1 */
#define NR_STATE_V2_LEN 32                  /* ... in v2 */
#define NR_SLOT_OTA_N     0x0f              /* how many OTA app slots the C6 has */
#define NR_SLOT_FACTORY   0x10              /* ... and a factory app partition */
#define NR_SLOT_NEXT_APP  0x20              /* the slot an update goes into holds a valid app now (what the update writes over) */
#define NR_SLOT_BACK_OK   0x40              /* an image written there would be turned back by its guard to the one running now */
#define NR_SLOT_REPORT    0x80              /* this is a report (a v2 relay's 0: not reported) */
_Static_assert(sizeof(nr_up_t) == 4 && sizeof(nr_ask_t) == 4 && sizeof(nr_state_t) == NR_STATE_V2_LEN + 12 && NR_STATE_V2_LEN == NR_STATE_V1_LEN + 4,
               "now_relay.h: the wire layout");
/* the host's rule for an answer (v2): it answers THIS request, or it is a late one to an earlier request and is not
   taken. A v1 relay's answer carries no number: it is taken, as v1 always did. (firmware/host_test/nr_host.c) */
static inline int nr_answer_is(const nr_state_t *st, unsigned len, uint16_t req) {
    if (len < NR_STATE_V1_LEN) return 0;                 /* not an answer at all */
    if (len < NR_STATE_V2_LEN || st->version < 2) return 1;   /* (v3: a v2 relay's 32 bytes carry the number too) */
    return st->req == req;
}
/* the version the host speaks to a relay that said `relay` (0: not heard yet - v1, which every relay takes). v3's
   requests are v2's: a v3 host speaks 2 to a v2 relay and 3 to a v3 */
static inline uint8_t nr_speak(uint8_t relay) { return relay >= 3 ? 3 : relay >= 2 ? 2 : 1; }
/* (v3, the host's check before it writes an image of `bytes` into the C6) 1: the C6 says an update fits and its guard
   could turn it back; 0: it says it could not; -1: it did not say (a relay older than v3, or factory firmware) */
static inline int nr_slots_ok(const nr_state_t *st, unsigned long bytes) {
    if (st->version < 3 || !(st->slots & NR_SLOT_REPORT)) return -1;
    if (st->next_sub == 0xff || st->next_sub == st->run_sub || !(st->slots & NR_SLOT_BACK_OK)) return 0;
    return (unsigned long)st->next_kb * 1024ul >= bytes;
}

enum { NR_OK = 0, NR_ERR_VERSION = 1, NR_ERR_WIFI = 2 /* Wi-Fi is not started: the host starts it first */,
       NR_ERR_NOW = 3 /* esp_now_init failed */, NR_ERR_CHANNEL = 4 };
