/* version.h - the tank's release number, the one place it lives (2026-09-29).
 *
 * Strato: "officially the tank should have a true version number so people
 * can easily understand what version they are on ... I would still consider
 * this alpha, pre 1.0". Everything shipped from the 09-11 launch to 09-29 is
 * the 0.1 series (the changelog keeps its git ids); 0.2.0 is the first
 * release under the batched schedule.
 *
 *   0.MINOR.0   a batched release (the number people talk about)
 *   0.x.PATCH   a fix that has to ship between releases
 *   1.0         Strato's call: stable saves, the core loop settled
 *
 * Bump it in the release commit, and write the changelog entry
 * (site/changelog.json) with the same number. The build id - the git
 * describe of the tree it was built from - rides beside it everywhere
 * (version_port_string), so a report still pins the exact build. The save
 * records PT_RELEASE_NUM of the build that wrote it (progression.c). */
#ifndef VERSION_H
#define VERSION_H

#define PT_RELEASE_MAJOR 0
#define PT_RELEASE_MINOR 3
#define PT_RELEASE_PATCH 3
#define PT_RELEASE       "0.3.3"
#define PT_RELEASE_STAGE "alpha"               /* pre-1.0: shown beside the number */
#define PT_RELEASE_NUM   ((PT_RELEASE_MAJOR << 16) | (PT_RELEASE_MINOR << 8) | PT_RELEASE_PATCH)

/* the model this build ships with (model/out/model_q4.bin): its tag, byte
 * length and sha256. The tank's model trailer (firmware/main/model_trailer.c)
 * is healed from these at boot when a cable install left none, and an
 * update manifest naming another model tag says "needs the cable" until the
 * model path lands (docs/OTA.md, phase two). tools/model_trailer.py --check
 * refuses a release whose model file disagrees with these. */
#define PT_MODEL_TAG     "v3m"
#define PT_MODEL_LEN     7557640u
#define PT_MODEL_SHA256  "50bed3e16a6b11f3496dde21d8784da9067f40ec9f0a77cf97f60a95acb80f6b"

/* the board this build is for (2026-10-02: 0.3.0 ships three - docs/BOARDS.md).
 * One source tree, one release number, but an image per board: a tank fetches
 * its own board's update manifest (latest-<PT_BOARD>.json), refuses a manifest
 * that names another board, and refuses an image whose board marker
 * (firmware/main/net_port_esp.c, right after the app descriptor) is not its
 * own. The installer offers one manifest per board. PT_BOARD is a file-name
 * safe id; PT_BOARD_NAME is the maker's name for it. */
#if defined(TANK_ROUND)
#define PT_BOARD         "round175c"
#define PT_BOARD_NAME    "ESP32-S3-Touch-AMOLED-1.75C"
#elif defined(TANK_WATCH)
#define PT_BOARD         "watch206"
#define PT_BOARD_NAME    "ESP32-S3-Touch-AMOLED-2.06"
#elif defined(CONFIG_POCKET_TANK_WST_320X240)
/* (this fork) the Waveshare ESP32-S3-Touch-LCD-2, the second 320 x 240 board (2026-10-08) */
#define PT_BOARD         "wst_320x240"
#define PT_BOARD_NAME    "ESP32-S3-Touch-LCD-2"
#elif defined(CONFIG_POCKET_TANK_320X240)
/* (this fork) the CYD, and the sim's 320 x 240 world, which names no board */
#define PT_BOARD         "cyd_320x240"
#define PT_BOARD_NAME    "ES3C28P"
#else
#define PT_BOARD         "amoled18"
#define PT_BOARD_NAME    "ESP32-S3-Touch-AMOLED-1.8"
#endif
#define PT_BOARD_MAGIC   "PTBOARD"             /* the image's board marker begins with these 8 bytes (with the NUL) */
#define PT_BOARD_MARKER_OFFSET 0x120           /* ... at this offset in the app image: the image header (24) + the
                                                  first segment's header (8) + esp_app_desc_t (256) */

#endif
