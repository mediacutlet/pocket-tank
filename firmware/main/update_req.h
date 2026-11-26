/* update_req.h - the words that ask the next boot for update mode (update_mode.c), on their own so the host can
 * test them (sim --selftest-updui H). Two words in RTC memory that survive esp_restart: the request and its
 * complement. "OTAz" = CHECK FOR UPDATES; "OTAa" (0.4.2) = ADD A NETWORK, the same mode begun at the list of
 * networks. Anything else - a cold boot's noise, one word without the other - is no request. Taking a request
 * clears both words: it is good for one boot, and *add is this boot's alone (never left over from the last). */
#ifndef UPDATE_REQ_H
#define UPDATE_REQ_H
#include <stdbool.h>
#include <stdint.h>
#define UPD_REQ_MAGIC     0x4f54417au      /* "OTAz" */
#define UPD_REQ_MAGIC_ADD 0x4f544161u      /* "OTAa" */
static inline void update_req_write(uint32_t *req, uint32_t *check, bool add) {
    const uint32_t m = add ? UPD_REQ_MAGIC_ADD : UPD_REQ_MAGIC;
    *req = m; *check = ~m;
}
static inline bool update_req_take(uint32_t *req, uint32_t *check, bool *add) {
    const bool yes = (*req == UPD_REQ_MAGIC || *req == UPD_REQ_MAGIC_ADD) && *check == ~*req;
    *add = yes && *req == UPD_REQ_MAGIC_ADD;
    *req = 0; *check = 0;                  /* one boot only, whatever happens next */
    return yes;
}
#endif
