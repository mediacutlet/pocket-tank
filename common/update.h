/* update.h - over-the-air updates: the pages and the flow (docs/OTA.md,
 * 2026-09-30). Platform-independent, like every page in common/: the
 * radio, the credential store and the download are PORTS (net_port_*),
 * implemented by firmware/main/net_port_esp.c on the tank and by
 * sim/net_port_sim.c with pretend networks on the desktop, so the pages are
 * built and self-tested in the sim like everything else.
 *
 * Two things live here:
 *
 * 1. THE UPDATES PAGE in the tank (from the settings page's UPDATES
 *    button), two faces: UPDATES (the version, CHECK FOR UPDATES, the WI-FI
 *    door, BACK) and WI-FI (the saved networks, up to three since 0.4.2,
 *    wifi_nets.h: a row each with its own FORGET; ADD A NETWORK; BACK). No
 *    radio. CHECK and ADD hand the platform UPD_TAP_CHECK / UPD_TAP_ADD;
 *    the tank saves and restarts into update mode.
 *
 * 2. UPDATE MODE, at boot, before the tank exists (main.c): the display,
 *    the glass and the radio only, so the Wi-Fi driver has the whole
 *    internal heap. update_begin decides the first page (the battery gate,
 *    a saved network or the wizard), update_tick polls the port and
 *    advances, update_touch takes the finger every frame, render_update
 *    draws. The platform loops until update_outcome says BACK (radio off,
 *    fall through to the normal boot) or RESTART (the new image is in the
 *    other slot; restart into it). Nothing here turns the radio off by
 *    itself: net_port_off is the platform's, called on the way out.
 *
 * The wizard: LOOKING FOR NETWORKS -> the list (name, signal bars, OPEN) ->
 * the password keyboard (7 x 3 big keys, letters on two pages with a case
 * toggle, digits and every printable symbol on three more: a WPA passphrase
 * is up to 63 of any printable character) -> CONNECTING -> CHECKING -> the
 * offer (VERSION X IS READY, the note, YES / NOT NOW) -> DOWNLOADING with a
 * bar and CANCEL -> RESTARTING. Every failure is a short message with TRY
 * AGAIN / OTHER NETWORK / BACK TO TANK, and leaves the old tank as it was. */
#ifndef UPDATE_H
#define UPDATE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---- the net port (platform) ---- */
#define NET_SSID_MAX   32
#define NET_PASS_MAX   63
#define NET_SCAN_MAX   12
#define NET_URL_MAX    160
typedef struct { char ssid[NET_SSID_MAX + 1]; int rssi; bool secured; } net_ap_t;
typedef struct {
    char     release[16];        /* "0.3.0" */
    uint32_t release_num;        /* PT_RELEASE_NUM's encoding */
    char     build[24];          /* the git build id */
    uint32_t min_from;           /* the oldest release this one updates from (0 = any) */
    char     note[72];           /* one plain line for the offer page */
    uint32_t app_size;           /* bytes */
    bool     needs_cable;        /* the model changed and this build cannot fetch it */
    char     model_version[16];  /* the model this release needs ("" = unchanged) */
    bool     model_offered;      /* phase two: the model is downloadable */
    uint32_t model_size;
    char     board[24];          /* the board the image is for (PT_BOARD); another board's manifest is refused */
} update_manifest_t;

enum { NET_IDLE = 0, NET_BUSY, NET_DONE, NET_FAILED };
enum { NET_ERR_NONE = 0, NET_ERR_NO_SIGNAL, NET_ERR_PASSWORD, NET_ERR_NO_NET, NET_ERR_BAD_MANIFEST,
       NET_ERR_DOWNLOAD, NET_ERR_VERIFY, NET_ERR_SPACE, NET_ERR_ABORTED, NET_ERR_RADIO,
       NET_ERR_BOARD };            /* the image's board marker named another board (2026-10-02): refused, nothing written past its first sector */

/* THE SAVED NETWORKS (0.4.2: up to three, wifi_nets.h - kept in the order the tank tries them, the one used last
 * first). _get is that first one, as it was when the tank kept one. _set ADDS (a name already saved takes the new
 * password and no new place; a fourth pushes out the one used longest ago - _replaces says which, before).
 * _forget is EVERY saved network (a full reset's call); _forget_one is one, the others kept in their order. */
#define NET_SAVED_MAX  3
bool net_port_creds_get(char *ssid, char *pass);            /* the one tried first (buffers NET_*_MAX + 1); false = none saved */
int  net_port_creds_count(void);
bool net_port_creds_at(int i, char *ssid, bool *joined);    /* the i-th in the order they are tried; joined: the tank has been on it */
bool net_port_creds_replaces(const char *ssid, char *victim);   /* saving ssid now would push victim out (NET_SSID_MAX + 1) */
bool net_port_creds_set(const char *ssid, const char *pass);
bool net_port_creds_forget_one(const char *ssid);           /* false = not saved */
bool net_port_creds_forget(void);                            /* EVERY saved network. false = the store would not open or take it: a network is still there */
/* the Hatchery Crew (docs/hatchery-crew.md, 0.4.1). The BOARD CODE is the
 * board's, never the save's: made once from the hardware RNG, 8 characters
 * of NET_CODE_ALPHABET shown as XXXX-XXXX, kept beside the Wi-Fi store (a
 * RESET keeps it). net_port_crew is the last crew check's answer, so the
 * UPDATES page can say HATCHERY CREW without the radio: 1 on the list, 0 not
 * on it or never asked. */
#define NET_CODE_LEN       9                                 /* "KM4T-7RXA" */
#define NET_CODE_ALPHABET  "ACDEFGHJKMNPQRTUVWXY34679"      /* no 0/O, 1/I/L, 2/Z, 5/S, 8/B */
bool net_port_board_code(char out[NET_CODE_LEN + 1]);       /* false only if the store can't be written */
int  net_port_crew(void);
/* the radio. Every call returns at once; the flow polls the *_state calls
 * each frame (NET_BUSY / NET_DONE / NET_FAILED, the reason in
 * net_port_fail_reason). One operation at a time. */
void net_port_scan_start(void);
int  net_port_scan_state(void);
int  net_port_scan_results(net_ap_t *out, int max);          /* after NET_DONE: strongest first, names unique */
/* (after NET_DONE, several saved networks: net_port_scan_saved, wifi_nets.h) */
void net_port_connect_start(const char *ssid, const char *pass);
int  net_port_connect_state(void);
void net_port_check_start(void);                            /* fetch the manifest (the platform knows the URL: update_target_board's) */
int  net_port_check_state(const update_manifest_t **m);     /* NET_DONE: *m valid until the next call */
void net_port_install_start(void);                          /* download, verify, arm the other slot */
int  net_port_install_state(int *percent);                  /* NET_BUSY with the percent; NET_DONE = ready to restart */
void net_port_abort(void);                                  /* stop a download (the flow reports NET_ERR_ABORTED) */
void net_port_off(void);                                    /* the radio off, the driver gone: the platform's way out */
int  net_port_fail_reason(void);
/* the message page's title (tests ask which message is up) */
const char *update_message_title(void);
const char *update_message_line1(void);                     /* ... and its first line (a network's name, on the pages that name one) */
const char *update_ssid(void);                              /* the network update mode is working with (tests) */

/* ---- the UPDATES page in the tank ---- */
/* TWO FACES (0.4.2, Strato on the boards: "the wifi screen is starting to look very cluttered.. buttons of different
 * sizes, fonts of different sizes, unbalanced"). The page did two jobs on one glass; now it is two plain pages in the
 * GRAPHICS page's style (the title centered, labels at the left, buttons of one height, one BACK centered in the foot):
 *   UPDATES   VERSION / BUILD / BOARD CODE as label and value, CHECK FOR UPDATES with its two quiet lines, and the
 *             WI-FI door (a settings tile: how many networks are saved, an arrow)
 *   WI-FI     the three places as a list - a saved network's name with its own FORGET (SURE? on the second tap, as
 *             before), an open place says EMPTY; a check mark stands before the one used last and LAST USED under the
 *             list says what the mark means (a shape and words, never a color); ADD A NETWORK; BACK (to UPDATES)
 * WI-FI is the UPDATES page's other face, as GRAPHICS is the settings page's: the ports show one page and feed one
 * touch function; the face turns inside updates_page_touch. A press that begins within OPEN_WAIT_US of the turn is
 * no tap (THE TOUCH RULE, touch_gate.h: a double tap on the door must not reach ADD A NETWORK under it, nor BACK's
 * double the UPDATES page's BACK). */
enum { UPD_TAP_NONE = 0, UPD_TAP_CLOSE,                     /* BACK on the UPDATES face: the port goes back to settings */
       UPD_TAP_CHECK,                                       /* the platform saves and restarts into update mode */
       UPD_TAP_NETS, UPD_TAP_NETS_BACK,                     /* the door opened / WI-FI's BACK: the face turned, nothing for the port to do */
       UPD_TAP_ADD,                                         /* ADD A NETWORK: the platform saves and restarts into update mode at the list (update_begin_add) */
       UPD_TAP_FORGET,                                      /* FORGET: the first network's row; + r for row r */
       UPD_TAP_FORGET_2, UPD_TAP_FORGET_3 };
void render_updates_page(uint16_t *fb, int stride);         /* the face that is up */
int  updates_page_tap(float x, float y);                    /* the bare hit test, on the face that is up (WI-FI's BACK reads UPD_TAP_NETS_BACK) */
int  updates_page_touch(float x, float y, bool down, int64_t now_us);   /* every frame: UPD_TAP_* on a tap's release; FORGET is applied
                                                                    here, on its second tap (SURE?) - now_us: the board's clock */
void updates_page_cancel(void);                             /* the port, each poll it does not feed the page: forget the press in hand */
void updates_page_open(void);                               /* the page comes up: the UPDATES face, FORGET not armed */
void updates_page_show_nets(bool on);                       /* (the director, the tests) the WI-FI face up, or the UPDATES face, with no tap */
bool updates_page_nets(void);                               /* the WI-FI face is the one up */
/* a row of the UPDATES face (VERSION / BUILD / BOARD CODE): where its value is drawn and what of it. In the values' column
 * when it fits; a longer one (an early build's "<commit>-early<n>", a dirty tree's) ends at the column's right edge and
 * starts no nearer the label than 14 px; one too long even for that (the app description holds 31 characters) is cut in
 * its middle, head...tail. Returns the PAGE x; out is what to draw */
int  updates_info_fit(const char *label, const char *value, char *out, size_t cap);
bool updates_forget_armed(void);
int  updates_forget_row(void);                              /* the row whose FORGET says SURE?, -1 = none */

/* the pages' layout (PAGE coordinates, render.h; 2026-10-01: out of update.c,
 * setup.h's way, so the sim's selftest taps where the layout says on every board) */
#define UPD_PANEL_W   384                       /* update mode's panel: x 32..416 */
/* both faces: one left edge and one width for everything (the bowl: 12 px further in, its circle is narrow at the first
 * rows), the settings page's title line and BACK in the settings foot's band (the watch: the title above the PAGE box and
 * the foot under it, the rows up by 44 - render.h, the settings page's own places). One button height: 40, the
 * settings segments'; BACK is the GRAPHICS page's (92 x the foot's 44). */
#define UPD_X         (PAGE_BOWL ? 44 : 32)
#define UPD_W         (PAGE_W - 2 * UPD_X)
#define UPD_OY        (SET_ROW1_Y - 58)         /* the rows' shift: 0, the watch -44 */
#define UPD_BTN40     40
#define UPD_CLOSE_W   92                        /* BACK (UPD_TAP_CLOSE; on the WI-FI face UPD_TAP_NETS_BACK) */
#define UPD_CLOSE_H   SET_NAV_SQ
#define UPD_CLOSE_X   ((PAGE_W - UPD_CLOSE_W) / 2)
#define UPD_CLOSE_Y   SET_NAV_Y
/* the UPDATES face */
#define UPD_INFO_Y    (56 + UPD_OY)             /* VERSION; BUILD and BOARD CODE (HATCHERY CREW) under it, UPD_INFO_DY apart */
#define UPD_INFO_DY   22
#define UPD_VAL_X     (UPD_X + 172)             /* the values' column (HATCHERY CREW is the longest label) */
#define UPD_CHECK_X   UPD_X
#define UPD_CHECK_Y   (128 + UPD_OY)
#define UPD_CHECK_W   UPD_W
#define UPD_CHECK_H   UPD_BTN40
#define UPD_TEXT_Y    (180 + UPD_OY)            /* WHILE UPDATING, THE BOARD / PAUSES AND REBOOTS ITSELF */
#define UPD_DOOR_Y    (228 + UPD_OY)            /* the WI-FI door: a tile across the page */
#define UPD_DOOR_H    UPD_BTN40
/* the WI-FI face: three places, UPD_NET_DY apart; a saved one's FORGET closes its row at the right */
#define UPD_NET_Y0    (52 + UPD_OY)
#define UPD_NET_DY    46
#define UPD_NET_H     UPD_BTN40
#define UPD_NAME_X    (UPD_X + 30)              /* the names, in from the row's edge: the last used one's check mark stands before it */
#define UPD_NAME_CHARS (PAGE_BOWL ? 19 : 21)    /* a name's room beside FORGET at scale 2; a longer one is cut in its middle (wifi_name_fit) */
#define UPD_FORGET_W  92
#define UPD_FORGET_X  (UPD_X + UPD_W - UPD_FORGET_W)
#define UPD_FORGET_Y  UPD_NET_Y0                /* the first row's; row r: + r * UPD_NET_DY */
#define UPD_FORGET_H  UPD_NET_H                 /* (its tap reaches 8 px to the sides and 3 up and down: half the gap to the next row's) */
#define UPD_LEGEND_Y  (196 + UPD_OY)            /* the check mark and LAST USED */
#define UPD_ADD_Y     (226 + UPD_OY)            /* ADD A NETWORK, UPD_X / UPD_W / UPD_BTN40 */
/* update mode: the keyboard's 7 x 3 keys, the foot's buttons, the network list */
#define UPD_KB_COLS 7
#define UPD_KB_ROWS 3
#define UPD_KB_PX   54
#define UPD_KB_PY   78
#define UPD_KB_W    50
#define UPD_KB_H    72
#define UPD_KB_X    37
#define UPD_KB_Y    96
#define UPD_BTN_H     38
#define UPD_BTN_Y     296
#define UPD_BTN_L_X   (PAGE_BOWL ? 56 : 32)       /* (the bowl: the pair drawn in from the glass - 0.4.2: 164 wide, 156 held */
#define UPD_BTN_R_X   (PAGE_BOWL ? 228 : 240)     /*  OTHER NETWORK with a pixel to spare at each end) */
#define UPD_BTN_W     (PAGE_BOWL ? 164 : 176)
#define UPD_BTN_MID_X ((PAGE_W - UPD_BTN_W) / 2)   /* a button on its own: CANCEL, a lone BACK TO TANK (the PLUG IN page's too,
                                                      Strato 2026-10-01; it stood at the left) - the page's middle
                                                      (until 2026-10-01 it was measured from the left button, which the bowl
                                                      draws in: its CANCEL sat 30 px right of center) */
#define UPD_ROW_Y0    58
#define UPD_ROW_H     38
#define UPD_ROWS_PER  6
#define UPD_ARROW_X   404

/* ---- update mode ---- */
enum { UPD_RUNNING = 0, UPD_BACK, UPD_RESTART };
enum { UPD_PG_NONE = 0, UPD_PG_POWER, UPD_PG_BUSY, UPD_PG_SCAN, UPD_PG_PASSWORD, UPD_PG_OFFER, UPD_PG_MESSAGE, UPD_PG_CALIB };
/* battery_pct < 0 = no gauge (allowed); on_power = the cable is in */
void update_begin(int battery_pct, bool on_power);
void update_begin_add(int battery_pct, bool on_power);     /* ADD A NETWORK (0.4.2): the same mode, begun at the list of networks in range -
                                                               the saved ones are not tried first; joined, the new one is saved and the check follows as ever */
void update_tick(float dt);
void update_touch(float x, float y, bool down);
void render_update(uint16_t *fb, int stride, double clock);
/* the clockless board's wake (2026-10-03): the glass while the boot asks the
 * internet for the time - a few seconds that would otherwise be dark, and a
 * dark glass after a press gets pressed again. Nothing to tap. */
void render_clock_sync(uint16_t *fb, int stride, double clock);
int  update_outcome(void);
bool update_active(void);
int  update_page(void);                                     /* UPD_PG_* (tests) */
int  update_hit(float x, float y);                          /* the page's element under (x,y), 0 = none (tests) */
const char *update_hit_name(int id);
const char *update_typed(void);                             /* the password field (tests) */
const char *update_power_title(void);                       /* the PLUG IN page's words: PLUG IN TO UPDATE / AN UPDATE NEEDS THE CABLE IN, or */
const char *update_power_line(void);                        /* for ADD A NETWORK (0.4.2) PLUG IN FIRST / THIS NEEDS THE CABLE IN */
void update_debug_keyboard(void);                           /* the keyboard page for a pretend network, the store untouched (touch calibration) */
void update_debug_calib(void);                              /* nine crosshairs, a press each: the platform logs where the panel says the finger was */
int  update_calib_target(int *x, int *y);                   /* the crosshair up (0..8, -1 = none), its centre */
#define UPD_BATTERY_MIN_PCT 20                              /* below this on battery: PLUG IN first */
#define UPD_UP_TO_DATE_S    8.0f                            /* the up-to-date page goes back by itself */

/* the case-sensitive 5x7 text (SSIDs and passwords are not upper case):
 * every printable ASCII character; the tank's own font stays as it is */
int  update_text_w(const char *s, int scale);
void update_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s);

/* the platform's one-line reason for the log */
const char *net_err_name(int err);
/* the board whose version this tank should run (0.3.9.3): the board it is, by
 * its chips, else this build's PT_BOARD - the manifest fetched, the image's
 * marker checked against it */
const char *update_target_board(void);
#endif
