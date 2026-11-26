/* c6_stage.h - the radio chip's firmware staged in a big screen's own flash, and the rules for handing it over
 * (0.4.3, the one install: Strato 11-24 "yes build toward this route", "automatic").
 *
 * The install page writes OUR relay image (firmware-c6) into the P4's flash with 0.4.3 - the app slot the tank does
 * not run from, ota_1 (radio_hosted.h C6_STAGE_OFFSET) - behind this 64-byte header. The tank checks it and hands it
 * to the C6 by itself (radio_hosted.c, the handover). Plain C: firmware/host_test/c6_stage_host.c runs it on the Mac.
 *
 * What makes an image good to hand over: the header's magic, version and length; a size that fits the C6's slot; the
 * image's own head says it is an ESP32-C6 app (its chip id) with an app description, and that it is our relay
 * (project "pocket_tank_c6": the automatic handover never writes anything else - the director's `radio c6 update`
 * may, for the factory stand-in's proof); and over the whole image the CRC32 and the SHA-256 the header carries. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define C6_STAGE_MAGIC2  0x32435450u        /* "PTC2": the v2 header (v1's "PTC6", 16 bytes, is not handed over by itself) */
#define C6_STAGE_HDR_VER 2
#define C6_STAGE_MAX     0x180000u          /* the C6's app slot: 1536 KB (firmware-c6/partitions.csv, as hosted 1.4.x lays it out) */
#define C6_STAGE_MIN     0x10000u
#define C6_CHIP_ID       13                 /* esp_image_header_t.chip_id: ESP_CHIP_ID_ESP32C6 */
#define C6_RELAY_PROJECT "pocket_tank_c6"

typedef struct __attribute__((packed)) {
    uint32_t magic;                         /* C6_STAGE_MAGIC2 */
    uint16_t hdr_ver, hdr_len;              /* 2, 64 */
    uint32_t size, crc32;                   /* the image's bytes, and their CRC32 (zlib's) */
    uint32_t relay_version;                 /* NR_VERSION of the relay inside (now_relay.h) */
    uint32_t relay_build;                   /* 0 for now (room for builds of one version) */
    uint8_t  sha256[32];                    /* SHA-256 of the image */
    uint8_t  reserved[8];
} c6_stage2_t;
_Static_assert(sizeof(c6_stage2_t) == 64, "c6_stage.h: the header is 64 bytes");

enum { C6S_OK, C6S_NO_STAGE, C6S_BAD_HEADER, C6S_BAD_SIZE, C6S_NOT_C6, C6S_NOT_APP, C6S_NOT_RELAY, C6S_BAD_VERSION };

/* the header and the image's first bytes (head: at least 112 of them): is this a C6 app, and ours? (the sums are the
   caller's: they need the whole image) */
static inline int c6_stage_check(const c6_stage2_t *h, const uint8_t *head, uint32_t head_n, bool relay_only) {
    if (h->magic != C6_STAGE_MAGIC2) return C6S_NO_STAGE;
    if (h->hdr_ver != C6_STAGE_HDR_VER || h->hdr_len != sizeof *h) return C6S_BAD_HEADER;
    if (h->size < C6_STAGE_MIN || h->size > C6_STAGE_MAX || head_n < 112) return C6S_BAD_SIZE;
    if (head[0] != 0xE9) return C6S_NOT_APP;                             /* an ESP image */
    if ((head[12] | head[13] << 8) != C6_CHIP_ID) return C6S_NOT_C6;      /* esp_image_header_t.chip_id */
    const uint32_t desc = (uint32_t)head[32] | (uint32_t)head[33] << 8 | (uint32_t)head[34] << 16 | (uint32_t)head[35] << 24;
    if (desc != 0xABCD5432u) return C6S_NOT_APP;                           /* esp_app_desc_t's magic, at the first segment's start */
    if (relay_only && strncmp((const char *)head + 80, C6_RELAY_PROJECT, 32) != 0) return C6S_NOT_RELAY;   /* project_name */
    if (relay_only) {                                    /* the image says its own relay version ("relay-v<N>", its app version): the
                                                            header must say the same (the 11-24 review: a version taken from elsewhere
                                                            labeled a v1 relay v2) */
        const char *v = (const char *)head + 48; unsigned n = 0; int k = 7;
        if (strncmp(v, "relay-v", 7) != 0 || v[7] < '0' || v[7] > '9') return C6S_BAD_VERSION;
        while (k < 32 && v[k] >= '0' && v[k] <= '9') n = n * 10 + (unsigned)(v[k++] - '0');
        if ((k < 32 && v[k] != 0) || n != h->relay_version) return C6S_BAD_VERSION;
    }
    return C6S_OK;
}

/* THE HANDOVER's rules, record and state: c6_ho.h (one pure function, checked exhaustively on the Mac) */
#include "c6_ho.h"
