#define _DEFAULT_SOURCE   /* setenv under -std=c11 (glibc) */
/* selftest_full_reset.c - --selftest-full-reset (0.4.2, docs/release-0.4.2.md item 12): THE RESET PROMPT'S TWO CHOICES.
 * The chord (BOOT 5 s + a tap) opens a choice, never a reset: RESET THIS TANK (the reset as it always was - the tank on the
 * glass only, the wallet, the other tanks and the receipts kept) or ERASE EVERYTHING (the board as a new install has it),
 * and BACK. Kids use these: ERASE EVERYTHING asks twice and its last answer is a 3 s HOLD; a tap never erases.
 *   A  the words fit their panels and buttons on this glass; each page draws exactly its panel, inside the glass (the
 *      round one: inside the circle)
 *   B  every button of every page: it has a place, on the glass, on something drawn; no other button is on that page
 *   C  the pages (render_reset_tap / render_reset_hold): the two-step path; the box unticked by default and every time
 *      the page comes; a tap on HOLD TO ERASE is nothing; a short hold is nothing and the bar empties; sliding off ends
 *      that press; only the full hold answers, 2 or 3 by the box; NO / BACK one tap from every page
 *   D  RESET THIS TANK, as before (progression_reset): the wallet, the other tanks, the receipts, SELECTIVE BREEDING and
 *      the Wi-Fi kept; the tank fresh; the same after a boot
 *   E  ERASE EVERYTHING (progression_erase_all): one fresh tank of two fry with the welcome owed, zero sand dollars,
 *      nothing unlocked, no receipts, no tanks or books on the shelf or the flash, an empty collection, no journal left;
 *      the same after a boot. Kept: the touch map, the board's settings (brightness, GRAPHICS' QUALITY, the VIGNETTE - D too),
 *      the board code and HATCHERY CREW. The Wi-Fi
 *      kept with the box unticked, forgotten with it ticked
 *   F  the first write refused: nothing erased, everything as it was
 *   G  THE POWER CUT at every write of the erase, then at every write of the boot after, then a clean boot: the old
 *      board whole (only when not even the journal landed) or the new board whole - never a wallet, a shelved tank, an
 *      unlock or a collected visitor on a board that was erased
 *   H  the firmware's touch port on the host (selftest_touch.c touch_port_reset_prompt): the same pages under its taps -
 *      the opening tap's double, a press from before a page, the 20 s give-up on each page, a mash on every button
 * The battery's history and log live only on the device (NVS `bat`, `tank`/bed): nothing here models them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tank.h"
#include "advisor.h"
#include "progression.h"
#include "render.h"
#include "shelf.h"
#include "collection.h"
#include "update.h"
#include "advanced.h"

const char *sim_shelf_dir(void);
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
int  shelf_port_sim_cut(int writes);
int  shelf_port_sim_fail(int writes);
int  shelf_port_sim_fail_n(int writes, int n);
void net_sim_forget_refuse(int n);
int  touch_port_reset_prompt(void);                  /* selftest_touch.c */
int  sim_device_u8(const char *key, int dflt);       /* main.c: the board's settings file (the boards' NVS keys outside the tank's save) */
void sim_device_u8_put(const char *key, int v);
/* THE BOARD'S OWN SETTINGS, set away from their defaults before a reset - brightness, GRAPHICS' QUALITY, the VIGNETTE (0.4.2:
   "vigoff") - and read afterwards as a boot reads them: both resets keep them */
/* THE BOARDS' SIDE, READ FROM THEIR SOURCE (the review of 662dd49: this file's resets cannot reach the sim's settings file, so the
   checks above say little about a board). The firmware's reset erases NAMED keys of NVS "tank" (persist_port_esp.c persist_port_erase);
   "gfx", "vigoff" and "bright" must never be among them, nothing may erase the namespace whole, and no other firmware source may erase
   one of them by name. And the director's `gfx` / `vignette` must hand their value to main's one save (touch_port_push_setting), as
   the page's tap does. NULL: all as it should be */
static const char *fw_source_wrong(void) {
    static char why[200], src[400000];
    static const char *const KEEP[3] = { "gfx", "vigoff", "bright" };
    static const char *const FILES[4] = { "../firmware/components/tankcore/persist_port_esp.c", "../firmware/main/main.c", "../firmware/main/director.c", "../firmware/main/touch_port_ft3168.c" };
    for (int f = 0; f < 4; f++) {
        FILE *fp = fopen(FILES[f], "r"); if (!fp) { snprintf(why, sizeof why, "cannot read %s (run from sim/)", FILES[f]); return why; }
        size_t n = fread(src, 1, sizeof src - 1, fp); fclose(fp); src[n] = 0;
        if (n >= sizeof src - 1) { snprintf(why, sizeof why, "%s is longer than this check reads", FILES[f]); return why; }
        if (f == 0) {
            char *fn = strstr(src, "bool persist_port_erase(void) {"), *end = fn ? strstr(fn, "\n}\n") : NULL;
            if (!fn || !end) { snprintf(why, sizeof why, "persist_port_erase not found in %s", FILES[f]); return why; }
            *end = 0;
            if (!strstr(fn, "nvs_erase_key(h, SAVE_NVS_KEY)") || !strstr(fn, "OWN[")) { snprintf(why, sizeof why, "persist_port_erase no longer erases a list of named keys: read it again"); return why; }
            if (strstr(fn, "nvs_erase_all")) { snprintf(why, sizeof why, "persist_port_erase erases the whole namespace: the board's settings go with the tank"); return why; }
            for (int k = 0; k < 3; k++) { char q[24]; snprintf(q, sizeof q, "\"%s\"", KEEP[k]);
                if (strstr(fn, q)) { snprintf(why, sizeof why, "persist_port_erase names the board's own key \"%s\"", KEEP[k]); return why; } }
            *end = '\n';
        }
        for (const char *p = src; (p = strstr(p, "nvs_erase_key(")); p++) {       /* any erase by name, anywhere in these files */
            const char *eol = strchr(p, '\n'); size_t len = eol ? (size_t)(eol - p) : strlen(p);
            for (int k = 0; k < 3; k++) { char q[24]; snprintf(q, sizeof q, "\"%s\"", KEEP[k]);
                const char *hit = strstr(p, q);
                if (hit && (size_t)(hit - p) < len && hit - p < 60) { snprintf(why, sizeof why, "%s erases the board's own key \"%s\"", FILES[f], KEEP[k]); return why; } }
        }
        if (f == 2 && (!strstr(src, "touch_port_push_setting(SET_TAP_VIG, render_vignette())") || !strstr(src, "touch_port_push_setting(SET_TAP_GFX, render_gfx_level())"))) {
            snprintf(why, sizeof why, "the director's `vignette` / `gfx` no longer hand their value to main's save (touch_port_push_setting)"); return why; }
        if (f == 1 && (!strstr(src, "nvs_u8_put(\"vigoff\", !v)") || !strstr(src, "render_set_vignette(nvs_u8(\"vigoff\", 0) != 1)"))) {
            snprintf(why, sizeof why, "main.c: the VIGNETTE's stored byte is not written as !ON and read as (byte != 1) = ON"); return why; }
    }
    return NULL;
}
static void dev_stage(void) { sim_device_u8_put("bright", 35); sim_device_u8_put("gfx", RENDER_GFX_MEDIUM); sim_device_u8_put("vigoff", 1); render_set_gfx_level(RENDER_GFX_HIGH); render_set_vignette(true); }
static const char *dev_wrong(void) {
    static char why[120];
    render_set_gfx_level(sim_device_u8("gfx", RENDER_GFX_HIGH)); render_set_vignette(sim_device_u8("vigoff", 0) != 1);   /* (main.c's boot) */
    const bool bad = sim_device_u8("bright", 0) != 35 || render_gfx_level() != RENDER_GFX_MEDIUM || render_vignette();
    snprintf(why, sizeof why, "the board's settings: brightness %d (want 35), QUALITY %s (want MEDIUM), the VIGNETTE %s (want OFF)", sim_device_u8("bright", 0), render_gfx_name(), render_vignette() ? "ON" : "OFF");
    render_set_gfx_level(RENDER_GFX_HIGH); render_set_vignette(true);
    return bad ? why : NULL;
}

static tank_t T;
static uint16_t fa[TANK_W * TANK_H];
static int fails;
#define FR_FAIL(...) do { printf("FAIL: full-reset: "); printf(__VA_ARGS__); printf("\n"); if (++fails > 12) return 1; } while (0)

/* ---- the board: its save, its storage, its frame, its Wi-Fi store and its board code, all in one scratch directory ---- */
static char s_dir[400];
static void board_env(void) {
    char p[600];
    snprintf(s_dir, sizeof s_dir, "%s-fullreset-%dx%d", sim_shelf_dir(), TANK_W, TANK_H);
    snprintf(p, sizeof p, "%s/tank.sav", s_dir); setenv("POCKET_TANK_SAVE", p, 1);
    snprintf(p, sizeof p, "%s/storage.bin", s_dir); setenv("POCKET_TANK_STORAGE", p, 1);
    snprintf(p, sizeof p, "%s/frame", s_dir); setenv("POCKET_TANK_FRAME", p, 1);
    snprintf(p, sizeof p, "%s/wifi.txt", s_dir); setenv("POCKET_TANK_WIFI", p, 1);
    snprintf(p, sizeof p, "%s/code.txt", s_dir); setenv("POCKET_TANK_BOARD_CODE_FILE", p, 1);
    unsetenv("POCKET_TANK_BOARD_CODE"); setenv("POCKET_TANK_CREW", "1", 1);
}
static void board_new(void) { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s/frame'", s_dir, s_dir); (void)system(cmd); }
static void dir_copy(const char *from, const char *to) { char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s' && cp -R '%s' '%s'", to, from, to); (void)system(cmd); }
static void boot(void) {                              /* main.c's order: the shelf, the tank, a swap's carry */
    tank_init(&T, 5);
    shelf_boot(clock_port_now_unix());
    progression_boot(&T);
    progression_swap_carry(&T);
}
static bool has_frame(const char *key) { uint8_t b[256]; size_t got = 0; return shelf_port_frame_get(key, b, sizeof b, &got); }
static const char *rescue_path(void) { static char p[700]; snprintf(p, sizeof p, "%s/storage.bin.rescue", s_dir); return p; }
static bool has_rescue(void) { FILE *f = fopen(rescue_path(), "rb"); if (f) fclose(f); return f != NULL; }
static bool has_wifi(void) { char s[NET_SSID_MAX + 1], p[NET_PASS_MAX + 1]; return net_port_creds_get(s, p); }
static int storage_marked(void) {                     /* slots on the "flash" that still wear a tank's or a book's magic */
    int n = 0; uint8_t m[4];
    for (int k = 0; k < (int)(shelf_port_size() / SHELF_SLOT_SIZE) && k < SHELF_SLOTS_MAX; k++)
        for (int s = 0; s < 2; s++)
            if (shelf_port_read((uint32_t)k * SHELF_SLOT_SIZE + (uint32_t)s * 4096, m, 4)) {
                uint32_t v = m[0] | m[1] << 8 | m[2] << 16 | (uint32_t)m[3] << 24;
                if (v == SHELF_BOOK_MAGIC || v == 0x31535450u) { n++; break; }   /* ("PTS1": shelf.c SHELF_MAGIC) */
            }
    return n;
}
static long area_nonblank(void) {                     /* bytes of the shelf's whole area that do not read erased: an honest erase leaves none */
    static uint8_t b[65536]; long n = 0; size_t got;
    FILE *f = fopen(getenv("POCKET_TANK_STORAGE"), "rb"); if (!f) return 0;    /* (never made: a board whose shelf was never touched) */
    while ((got = fread(b, 1, sizeof b, f)) > 0) for (size_t i = 0; i < got; i++) n += b[i] != 0xff;
    fclose(f);
    return n;
}
static bool collect(int j) {                          /* a visitor in slot 0, collected (selftest_coll.c's) */
    fish_t *v = &T.fish[N_FISH_MAX]; *v = T.fish[0];
    v->uid = 0x77000000u + (uint32_t)j; v->stage = (stage_t)(1 + j % 3);
    snprintf(v->name, sizeof v->name, "v%d", j);
    T.vis_used[0] = true; T.vis_badge[0] = (int8_t)(j % 4); T.vis_home[0] = 0x7100 + (uint32_t)j; T.vis_since[0] = T.clock;
    bool ok = collection_add(&T, 0);
    T.vis_used[0] = false;
    return ok;
}
/* A BOARD WELL USED: a tank with a save and a fresh one on the shelf, the one on the glass with a wallet, things bought,
   SELECTIVE BREEDING, receipts, twenty visitors collected (a visitor book on the shelf's flash), a Wi-Fi network */
#define W0      4321
#define COLL0   20
#define SHELF0  2
#define UNLOCK0 (SD_ITEM_CASTLE | SD_ITEM_SNAIL | SD_ITEM_BREED)
static const uint8_t RCPT0 = SD_RCPT_KNOWN | SD_RCPT_GRASS | (2u << SD_RCPT_TRUST_SH);
static bool rich_board(void) {
    board_new(); boot(); progression_setup_done(&T);
    T.sd_balance = 100; progression_save(&T);
    int64_t now = clock_port_now_unix();
    if (shelf_new(SHELF_TYPE_FISH, now) < 0 || shelf_new(SHELF_TYPE_FISH, now) < 0) return false;
    progression_carry_t c; progression_carry_take(&T, &c);
    progression_save(&T);
    static uint16_t snap[64 * 48]; for (int i = 0; i < 64 * 48; i++) snap[i] = (uint16_t)(0x1234 + i * 7);
    if (!shelf_swap(0, snap, 64, 48, &c, sizeof c, now)) return false;     /* the first tank onto the shelf with its save and its snapshot */
    boot(); progression_setup_done(&T);
    T.sd_balance = W0; T.sd_unlocks |= UNLOCK0; T.sd_receipts = RCPT0;
    for (int j = 0; j < COLL0; j++) if (!collect(j)) return false;
    if (!progression_save(&T)) return false;
    if (!net_port_creds_set("HomeNet", "fishfishfish")) return false;
    { FILE *f = fopen(rescue_path(), "wb"); if (!f) return false; fputs("an old NVS image: the saves, the Wi-Fi password", f); fclose(f); }
    T.light_auto = true; T.light_idle_s = 900; progression_save(&T);   /* LIGHTS OUT set by the keeper */
    if (!shelf_port_frame_set("move", "a fish in the travel bowl", 26)) return false;
    if (!shelf_type_paid_set(SHELF_TYPE_PIRANHA)) return false;            /* (0.4.2: an experimental tank paid for once - the board's `xbuy`) */   /* (0.4.2: the board's travel bowl - selftest_move.c has a real fish in it) */
    boot();
    return T.sd_balance == W0 && shelf_count() == SHELF0 && collection_count() == COLL0 && shelf_book_count() >= 1 && has_wifi();
}
static bool is_old(void) {
    return T.sd_balance == W0 && shelf_count() == SHELF0 && collection_count() == COLL0 && (T.sd_unlocks & UNLOCK0) == UNLOCK0
        && T.sd_receipts == RCPT0 && shelf_book_count() >= 1 && has_wifi() && has_rescue() && !progression_setup_pending();
}
static const char *new_wrong(bool wifi_gone) {        /* NULL = the board is a new one */
    if (T.sd_balance != 0) return "sand dollars left";
    if (T.sd_earned != 0) return "sand dollars earned left";
    if (T.sd_unlocks != 0 || T.sd_shelved != 0 || T.bg_now != 0) return "something bought or unlocked left";
    if (T.sd_receipts != SD_RCPT_KNOWN) return "receipts left";
    if (shelf_count() != 0) return "a tank left on the shelf";
    if (shelf_book_count() != 0) return "a visitor book left";
    if (storage_marked() != 0) return "a tank or a book left on the flash";
    if (area_nonblank() != 0) return "the shelf's area does not read blank (a snapshot, a book's payload, old bytes left)";
    if (collection_count() != 0) return "a collected visitor left";
    if (T.n_fish != 2 || T.fish[0].stage != STAGE_FRY || T.fish[1].stage != STAGE_FRY) return "not two fry";
    if (!progression_setup_pending()) return "the welcome is not owed";
    if (shelf_now()->id != 1 || shelf_now()->pos != 0 || shelf_now()->type != SHELF_TYPE_FISH || strcmp(shelf_now()->name, "MY TANK")) return "the tank on the glass is not MY TANK at the first place";
    if (has_frame("wipe")) return "ERASE EVERYTHING's journal left";
    if (has_frame("reset") || has_frame("swap") || has_frame("carry") || has_frame("buy") || has_frame("rm") || has_frame("move")) return "a shelf record left (the travel bowl's `move` is one)";
    if (has_frame("xbuy") || shelf_type_paid(SHELF_TYPE_PIRANHA)) return "an experimental tank still reads paid for (the board's `xbuy`)";
    if (has_rescue()) return "the NVS rescue copy (old saves, the Wi-Fi password) left";
    if (!T.light_auto || T.light_idle_s != 900) return "LIGHTS OUT's setting was not kept";
    if (has_wifi() == wifi_gone) return wifi_gone ? "the Wi-Fi was kept (the box was ticked)" : "the Wi-Fi was forgotten (the box was not ticked)";
    return NULL;
}

/* ---- the pages ---- */
#define SENT 0x1ff8
static bool on_glass(int x, int y) {
#ifdef TANK_ROUND
    float dx = x + 0.5f - TANK_W / 2.0f, dy = y + 0.5f - TANK_H / 2.0f, r = TANK_W / 2.0f - 4;
    return dx * dx + dy * dy <= r * r;
#else
    return x >= 0 && y >= 0 && x < TANK_W && y < TANK_H;
#endif
}
static const char *const PG_NAME[7] = { "none", "the choice", "RESET THIS TANK?", "ERASE EVERYTHING?", "ARE YOU SURE?", "COULD NOT ERASE", "ERASING" };
static const char *const BTN_NAME[8] = { "none", "BACK / NO", "RESET THIS TANK", "ERASE EVERYTHING", "YES", "the Wi-Fi box", "NEXT", "HOLD TO ERASE" };
static bool btn_at(int btn, float *ox, float *oy) {   /* the middle of a button's hit area on the page that is up */
    long sx = 0, sy = 0, n = 0;
    for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) if (render_reset_hit((float)x, (float)y) == btn) { sx += x; sy += y; n++; }
    if (!n) return false;
    *ox = (float)(sx / n); *oy = (float)(sy / n);
    return render_reset_hit(*ox, *oy) == btn;
}
static int tap_btn(int btn) { float x, y; if (!btn_at(btn, &x, &y)) return -99; return render_reset_tap(x, y, x, y); }
static int pages_drawn(void) {
    static const uint8_t WANT[7] = { 0, 1u << RESET_BTN_BACK | 1u << RESET_BTN_TANK | 1u << RESET_BTN_ERASE, 1u << RESET_BTN_BACK | 1u << RESET_BTN_YES,
                                     1u << RESET_BTN_BACK | 1u << RESET_BTN_WIFI | 1u << RESET_BTN_NEXT, 1u << RESET_BTN_BACK | 1u << RESET_BTN_HOLD, 1u << RESET_BTN_BACK, 0 };
    if (!render_reset_text_fits()) FR_FAIL("a line of the prompt is wider than its panel or button, or the panel is off this glass");
    for (int pg = RESET_PG_CHOOSE; pg <= RESET_PG_ERASING; pg++)
        for (int wifi = 0; wifi < 2; wifi++) {
            render_reset_open(pg); render_reset_set_wifi(wifi); if (pg == RESET_PG_HOLD) render_reset_set_hold(0.5f);
            for (int i = 0; i < TANK_W * TANK_H; i++) fa[i] = SENT;
            render_frame_begin(); render_reset_prompt(fa, TANK_W, 0.5f);
            int x0 = TANK_W, y0 = TANK_H, x1 = -1, y1 = -1, off = 0;
            for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) if (fa[y * TANK_W + x] != SENT) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
                if (!on_glass(x, y)) off++;
            }
            if (x1 < 0) FR_FAIL("%s drew nothing", PG_NAME[pg]);
            if (off) FR_FAIL("%s: %d pixels drawn off the glass", PG_NAME[pg], off);
            const int px = PAGE_X + (pg == RESET_PG_TANK ? RENDER_CONFIRM_X : RESET_PANEL_X), py = PAGE_Y + (pg == RESET_PG_TANK ? RENDER_CONFIRM_Y : RESET_PANEL_Y);
            const int pw = pg == RESET_PG_TANK ? RENDER_CONFIRM_W : RESET_PANEL_W, ph = pg == RESET_PG_TANK ? RENDER_CONFIRM_H : RESET_PANEL_H;
            if (x0 != px || y0 != py || x1 != px + pw - 1 || y1 != py + ph - 1)
                FR_FAIL("%s drew %d,%d..%d,%d, not its panel %d,%d..%d,%d", PG_NAME[pg], x0, y0, x1, y1, px, py, px + pw - 1, py + ph - 1);
            unsigned seen = 0;
            for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
                int b = render_reset_hit((float)x, (float)y);
                if (b) { seen |= 1u << b; if (!on_glass(x, y)) FR_FAIL("%s: %s answers off the glass at %d,%d", PG_NAME[pg], BTN_NAME[b], x, y); }
            }
            if (seen != WANT[pg]) FR_FAIL("%s: its buttons are %#x (want %#x)", PG_NAME[pg], seen, WANT[pg]);
            for (int b = 1; b < 8; b++) if (WANT[pg] & 1u << b) {
                float x, y;
                if (!btn_at(b, &x, &y)) { FR_FAIL("%s: %s has no place", PG_NAME[pg], BTN_NAME[b]); continue; }
                if (fa[(int)y * TANK_W + (int)x] == SENT) FR_FAIL("%s: %s answers at %.0f,%.0f where nothing is drawn", PG_NAME[pg], BTN_NAME[b], x, y);
                if (x < px || x >= px + pw || y < py || y >= py + ph) FR_FAIL("%s: %s's middle %.0f,%.0f is outside the panel", PG_NAME[pg], BTN_NAME[b], x, y);
            }
            /* the box and the hold's bar change the picture: a ticked box, a hold under way, are SEEN */
            if (pg == RESET_PG_ERASE || pg == RESET_PG_HOLD) {
                static uint16_t fo[TANK_W * TANK_H];
                render_reset_set_wifi(!wifi); if (pg == RESET_PG_HOLD) { render_reset_set_wifi(wifi); render_reset_set_hold(0); }
                for (int i = 0; i < TANK_W * TANK_H; i++) fo[i] = SENT;
                render_frame_begin(); render_reset_prompt(fo, TANK_W, 0.5f);
                int diff = 0; for (int i = 0; i < TANK_W * TANK_H; i++) diff += fo[i] != fa[i];
                if (diff < 200) FR_FAIL("%s: %s changes only %d pixels - it would not be seen", PG_NAME[pg], pg == RESET_PG_ERASE ? "the box ticked" : "a hold half way", diff);
            }
        }
    render_reset_close();
    return 0;
}
/* THE PROMPT BAKED (render.c): a page kept as a picture and copied in, the bar and the hold's fill drawn over it, is the
   page drawn whole - pixel for pixel, every page, the box both ways, the hold and the timeout at any length, and across
   every change of page; the picture is lent (taken when the prompt is first drawn, given back when it has been down a
   while), and ERASING takes none */
static int s_bk_allocs, s_bk_frees;
static bool s_bk_refuse;
static void *bk_alloc(size_t n) { s_bk_allocs++; return s_bk_refuse ? NULL : malloc(n); }
static void bk_free(void *p) { s_bk_frees++; free(p); }
static void page_frame(uint16_t *fb, float frac) { for (int i = 0; i < TANK_W * TANK_H; i++) fb[i] = SENT; render_frame_begin(); render_reset_prompt(fb, TANK_W, frac); }
static int pages_baked(void) {
    static uint16_t fo[TANK_W * TANK_H];
    static const float HOLD[4] = { 0, 0.02f, 0.37f, 1.0f }, FRAC[3] = { 1.0f, 0.43f, 0.0f };
    int n = 0;
    s_bk_allocs = s_bk_frees = 0;
    for (int round = 0; round < 2; round++)                      /* (twice: the second time every page's picture replaces another's) */
        for (int pg = RESET_PG_CHOOSE; pg <= RESET_PG_ERASING; pg++)
            for (int wifi = 0; wifi < 2; wifi++)
                for (int h = 0; h < (pg == RESET_PG_HOLD ? 4 : 1); h++)
                    for (int f = 0; f < 3; f++) {
                        render_reset_open(pg); render_reset_set_wifi(wifi); render_reset_set_hold(HOLD[h]);
                        render_set_modal_alloc(bk_alloc, bk_free);
                        render_modal_cache_on(false); page_frame(fa, FRAC[f]);    /* drawn whole (the director's `link bake off`: no picture used) */
                        render_modal_cache_on(true); page_frame(fo, FRAC[f]); page_frame(fo, FRAC[f]);   /* from the picture (painted if it changed, then copied) */
                        if (pg != RESET_PG_ERASING && !render_reset_bake_held()) FR_FAIL("%s: no picture was kept of it (the bake is not in use)", PG_NAME[pg]);
                        int diff = 0, dx0 = TANK_W, dy0 = TANK_H, dx1 = -1, dy1 = -1;
                        for (int i = 0; i < TANK_W * TANK_H; i++) if (fo[i] != fa[i]) { diff++; int x = i % TANK_W, y = i / TANK_W; if (x < dx0) dx0 = x; if (x > dx1) dx1 = x; if (y < dy0) dy0 = y; if (y > dy1) dy1 = y; }
                        if (diff) printf("full-reset: (the difference: %d,%d..%d,%d, page coordinates %d,%d)\n", dx0, dy0, dx1, dy1, dx0 - PAGE_X, dy0 - PAGE_Y);
                        if (diff) FR_FAIL("%s (box %d, hold %.2f, timeout %.2f): from its picture it differs from drawn whole in %d pixels", PG_NAME[pg], wifi, HOLD[h], FRAC[f], diff);
                        n++;
                    }
    if (s_bk_allocs != 1) FR_FAIL("the prompt's picture was taken %d times over %d frames (want once: it is kept while the prompt is up)", s_bk_allocs, n);
    render_reset_close();
    for (int k = 0; k < 200; k++) render_frame_begin();           /* the prompt down a while: the picture goes back */
    if (render_reset_bake_held() || s_bk_frees != 1) FR_FAIL("the prompt down 200 frames: its picture was not given back (%d frees)", s_bk_frees);
    /* the lend REFUSED (no PSRAM to spare): the page painted whole, as before the bake - and asked for again later, not every frame */
    s_bk_allocs = 0; s_bk_refuse = true;
    render_reset_open(RESET_PG_CHOOSE);
    render_modal_cache_on(false); page_frame(fa, 0.5f); render_modal_cache_on(true);
    for (int k = 0; k < 10; k++) page_frame(fo, 0.5f);
    {   int diff = 0; for (int i = 0; i < TANK_W * TANK_H; i++) diff += fo[i] != fa[i];
        if (diff || render_reset_bake_held()) FR_FAIL("the picture's memory refused: the page differs from drawn whole in %d pixels, or a picture is held", diff);
        if (s_bk_allocs != 1) FR_FAIL("the picture's memory refused: asked %d times in 10 frames (want once, then a while later)", s_bk_allocs); }
    s_bk_refuse = false;
    for (int k = 0; k < 60; k++) page_frame(fo, 0.5f);
    {   int diff = 0; for (int i = 0; i < TANK_W * TANK_H; i++) diff += fo[i] != fa[i];
        if (diff || !render_reset_bake_held()) FR_FAIL("the picture's memory there again: %s (%d pixels differ)", render_reset_bake_held() ? "taken" : "never asked for again", diff); }
    render_reset_close(); for (int k = 0; k < 200; k++) render_frame_begin();
    s_bk_allocs = 0;
    render_reset_open(RESET_PG_ERASING); page_frame(fo, 0); page_frame(fo, 0);
    if (s_bk_allocs || render_reset_bake_held()) FR_FAIL("ERASING took a picture's memory (%d): it must take none on its way to the erase", s_bk_allocs);
    render_reset_close(); render_set_modal_alloc(NULL, NULL); render_modal_cache_on(true);
    return 0;
}
/* ERASING (the page before the erase). ARMED (the real one): nothing on it answers, and the erase is handed over only once
   it has been drawn. STAGED by name with no erase behind it (the director's): only a picture - a tap takes it down.
   No arm outlives its page: opening or closing the prompt disarms */
static int page_erasing(void) {
    render_reset_open(RESET_PG_ERASING);
    for (int y = 0; y < TANK_H; y += 3) for (int x = 0; x < TANK_W; x += 3) if (render_reset_hit((float)x, (float)y)) FR_FAIL("ERASING: a button answers at %d,%d (it has none)", x, y);
    for (int k = 0; k < 500; k++) if (render_reset_hold(true, TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2.0f, TANK_H / 2.0f, 0.04f)) FR_FAIL("ERASING: a 20 s hold on it answered");
    page_frame(fa, 0); page_frame(fa, 0); page_frame(fa, 0);
    if (render_reset_erase_ready() || render_reset_erase_armed()) FR_FAIL("ERASING staged by name (the director's), drawn three times: it asked for an erase");
    if (render_reset_tap(TANK_W / 2.0f, TANK_H / 2.0f, TANK_W / 2.0f, TANK_H / 2.0f) != RESET_ANS_NO || render_reset_page() != RESET_PG_NONE)
        FR_FAIL("ERASING staged by name: a tap did not take it down (page %d) - it would hold the glass until a command", render_reset_page());
    for (int wifi = 0; wifi < 2; wifi++) {
        /* the whole way: the choice, the page, the box, the hold - its answer arms the erase and brings ERASING */
        render_reset_open(RESET_PG_CHOOSE); tap_btn(RESET_BTN_ERASE); if (wifi) tap_btn(RESET_BTN_WIFI); tap_btn(RESET_BTN_NEXT);
        float hx, hy; btn_at(RESET_BTN_HOLD, &hx, &hy);
        int r = 0; for (int k = 0; k < 200 && !r; k++) r = render_reset_hold(true, hx, hy, hx, hy, 0.04f);
        const int want = wifi ? RESET_ANS_ERASE_WIFI : RESET_ANS_ERASE;
        if (r != want) FR_FAIL("the hold did not answer (%d)", r);
        render_reset_erasing(r);
        if (render_reset_page() != RESET_PG_ERASING || !render_reset_erase_armed()) FR_FAIL("the hold's answer did not bring ERASING (page %d)", render_reset_page());
        for (int y = 0; y < TANK_H; y += 7) for (int x = 0; x < TANK_W; x += 7)
            if (render_reset_tap((float)x, (float)y, (float)x, (float)y) || render_reset_page() != RESET_PG_ERASING || !render_reset_erase_armed()) FR_FAIL("ERASING, armed: a tap at %d,%d did something", x, y);
        for (int k = 0; k < 100; k++) if (render_reset_erase_ready()) FR_FAIL("the erase was handed over before ERASING was ever drawn (ask %d)", k);
        for (int d = 1; d < RESET_ERASING_FRAMES; d++) {
            page_frame(fa, 0);
            if (render_reset_erase_ready()) FR_FAIL("the erase was handed over after ERASING was drawn %d time(s) (want %d: one whole frame certainly on the glass)", d, RESET_ERASING_FRAMES);
        }
        page_frame(fa, 0);
        int px = 0; for (int i = 0; i < TANK_W * TANK_H; i++) px += fa[i] != SENT;
        if (px < 50000) FR_FAIL("ERASING's frame before the erase holds %d drawn pixels - the page is not on it", px);
        if ((r = render_reset_erase_ready()) != want) FR_FAIL("ERASING drawn %d times: the erase handed over is %d (want %d)", RESET_ERASING_FRAMES, r, want);
        if (render_reset_erase_ready() || render_reset_erase_armed()) FR_FAIL("the erase was handed over twice");
        if (render_reset_page() != RESET_PG_ERASING) FR_FAIL("ERASING left the glass before the erase (page %d): the board goes quiet under it", render_reset_page());
        /* no arm outlives its page: closed, or the prompt opened again, under an armed erase - disarmed, nothing handed over, ever */
        render_reset_erasing(want); render_reset_close();
        if (render_reset_erase_armed()) FR_FAIL("the prompt closed under an armed erase: still armed");
        for (int k = 0; k < 5; k++) { render_reset_open(RESET_PG_ERASING); page_frame(fa, 0); page_frame(fa, 0); page_frame(fa, 0); }
        if (render_reset_erase_ready()) FR_FAIL("the prompt closed under an armed erase, ERASING staged again later: the old erase was handed over");
        render_reset_erasing(want); render_reset_open(RESET_PG_CHOOSE);
        if (render_reset_erase_armed() || render_reset_erase_ready()) FR_FAIL("the prompt opened again under an armed erase: still armed");
        tap_btn(RESET_BTN_BACK); page_frame(fa, 0);
        if (render_reset_erase_ready()) FR_FAIL("BACK on the choice, opened over an armed erase: the erase was handed over");
        render_reset_close();
    }
    return 0;
}
static int pages_work(void) {
    float hx = 0, hy = 0, nx = 0, ny = 0;
    /* a way out on every page, one tap */
    for (int pg = RESET_PG_CHOOSE; pg <= RESET_PG_REFUSED; pg++) {
        render_reset_open(pg);
        if (pg == RESET_PG_REFUSED) { float x, y; btn_at(RESET_BTN_BACK, &x, &y); for (int k = 0; k < 200; k++) if (render_reset_hold(true, x, y, x, y, 0.04f)) FR_FAIL("COULD NOT ERASE: a hold on OK answered"); render_reset_hold(false, 0, 0, 0, 0, 0); }
        int r = tap_btn(RESET_BTN_BACK);
        if (pg == RESET_PG_ERASE) { if (r != RESET_EV_PAGE || render_reset_page() != RESET_PG_CHOOSE) FR_FAIL("ERASE EVERYTHING?: BACK gave %d, page %d (want the choice again)", r, render_reset_page()); }
        else if (r != RESET_ANS_NO || render_reset_page() != RESET_PG_NONE) FR_FAIL("%s: its BACK / NO gave %d, page %d (want NO, the prompt closed)", PG_NAME[pg], r, render_reset_page());
    }
    /* RESET THIS TANK: the choice, then YES */
    render_reset_open(RESET_PG_CHOOSE);
    if (tap_btn(RESET_BTN_TANK) != RESET_EV_PAGE || render_reset_page() != RESET_PG_TANK) FR_FAIL("the choice: RESET THIS TANK did not bring its page (%d)", render_reset_page());
    if (render_reset_hold(true, 0, 0, 0, 0, 10) || render_reset_page() != RESET_PG_TANK) FR_FAIL("RESET THIS TANK?: a 10 s hold answered");
    if (tap_btn(RESET_BTN_YES) != RESET_ANS_TANK || render_reset_page() != RESET_PG_NONE) FR_FAIL("RESET THIS TANK?: YES did not answer");
    /* ERASE EVERYTHING: the choice, its page (the box unticked), NEXT, the hold */
    render_reset_open(RESET_PG_CHOOSE);
    {   float x, y; btn_at(RESET_BTN_ERASE, &x, &y);
        if (render_reset_hold(true, x, y, x, y, 30) || render_reset_page() != RESET_PG_CHOOSE) FR_FAIL("the choice: ERASE EVERYTHING held 30 s answered (only its last page's hold may)");
        render_reset_hold(false, 0, 0, 0, 0, 0);
        float bx, by; btn_at(RESET_BTN_TANK, &bx, &by);
        if (render_reset_tap(x, y, bx, by) != RESET_ANS_NONE || render_reset_page() != RESET_PG_CHOOSE) FR_FAIL("the choice: a press on one button lifted on the other did something"); }
    if (tap_btn(RESET_BTN_ERASE) != RESET_EV_PAGE || render_reset_page() != RESET_PG_ERASE) FR_FAIL("the choice: ERASE EVERYTHING did not bring its page (%d)", render_reset_page());
    if (render_reset_wifi()) FR_FAIL("ERASE EVERYTHING?: ALSO FORGET WI-FI came ticked");
    if (tap_btn(RESET_BTN_WIFI) != RESET_EV_PAGE || !render_reset_wifi() || render_reset_page() != RESET_PG_ERASE) FR_FAIL("the Wi-Fi box: a tap did not tick it");
    if (tap_btn(RESET_BTN_WIFI) != RESET_EV_PAGE || render_reset_wifi()) FR_FAIL("the Wi-Fi box: a second tap did not untick it");
    tap_btn(RESET_BTN_WIFI);
    if (tap_btn(RESET_BTN_BACK) != RESET_EV_PAGE || tap_btn(RESET_BTN_ERASE) != RESET_EV_PAGE || render_reset_wifi()) FR_FAIL("the Wi-Fi box: ticked, BACK, ERASE EVERYTHING again - it came back ticked");
    {   float x, y; btn_at(RESET_BTN_NEXT, &x, &y);
        if (render_reset_hold(true, x, y, x, y, 30) || render_reset_page() != RESET_PG_ERASE) FR_FAIL("ERASE EVERYTHING?: NEXT held 30 s answered"); render_reset_hold(false, 0, 0, 0, 0, 0); }
    if (tap_btn(RESET_BTN_NEXT) != RESET_EV_PAGE || render_reset_page() != RESET_PG_HOLD) FR_FAIL("ERASE EVERYTHING?: NEXT did not bring the last page (%d)", render_reset_page());
    if (render_reset_hold_frac() != 0) FR_FAIL("ARE YOU SURE?: the bar came part full");
    if (!btn_at(RESET_BTN_HOLD, &hx, &hy) || !btn_at(RESET_BTN_BACK, &nx, &ny)) FR_FAIL("ARE YOU SURE?: no HOLD TO ERASE or no NO");
    /* a tap on it: nothing, however many */
    #define HOLD_N(sec) ((int)((sec) / 0.04f + 0.5f))
    for (int k = 0; k < 50; k++) {
        if (render_reset_hold(true, hx, hy, hx, hy, 0.04f) || render_reset_hold(true, hx, hy, hx, hy, 0.04f)) FR_FAIL("a tap's 80 ms on HOLD TO ERASE erased");
        render_reset_hold(false, hx, hy, hx, hy, 0.04f);
        if (render_reset_tap(hx, hy, hx, hy) != RESET_ANS_NONE || render_reset_page() != RESET_PG_HOLD) FR_FAIL("a tap on HOLD TO ERASE did something (tap %d)", k);
    }
    /* a short hold: the bar runs, then empties when the finger lifts */
    int r = 0, k;
    for (k = 0; k < HOLD_N(RESET_HOLD_S - 0.2f); k++) r |= render_reset_hold(true, hx, hy, hx, hy, 0.04f);
    if (r || !render_reset_holding()) FR_FAIL("a %.1f s hold: answered %d, or not seen as a hold", RESET_HOLD_S - 0.2f, r);
    if (render_reset_hold_frac() < 0.9f) FR_FAIL("a %.1f s hold: the bar shows %.2f", RESET_HOLD_S - 0.2f, render_reset_hold_frac());
    if (render_reset_hold(false, hx, hy, hx, hy, 0.04f) || render_reset_hold_frac() != 0 || render_reset_holding() || render_reset_page() != RESET_PG_HOLD)
        FR_FAIL("let go early: answered, or the bar stayed (%.2f), or the page went", render_reset_hold_frac());
    /* A STALLED TASK: the finger read on the button once after a minute of no polls is a step, not a hold; ten such, a second */
    if (render_reset_hold(true, hx, hy, hx, hy, 60.0f) || render_reset_hold_frac() > RESET_HOLD_STEP_S / RESET_HOLD_S + 0.001f)
        FR_FAIL("one poll after a 60 s stall: answered, or the bar jumped to %.2f (a poll adds %.1f s at most)", render_reset_hold_frac(), RESET_HOLD_STEP_S);
    r = 0; for (k = 0; k < 9; k++) r |= render_reset_hold(true, hx, hy, hx, hy, 5.0f);
    if (r || render_reset_page() != RESET_PG_HOLD) FR_FAIL("ten polls 5 s apart (50 s 'held', 1 s seen): answered %d", r);
    render_reset_hold(false, 0, 0, 0, 0, 0.04f);
    /* slid off the button: that press is done, back on it or not */
    r = 0;
    for (k = 0; k < HOLD_N(1.0f); k++) r |= render_reset_hold(true, hx, hy, hx, hy, 0.04f);
    r |= render_reset_hold(true, hx, hy, nx, ny, 0.04f);
    if (render_reset_hold_frac() != 0) FR_FAIL("slid off HOLD TO ERASE: the bar stayed");
    for (k = 0; k < HOLD_N(8.0f); k++) r |= render_reset_hold(true, hx, hy, hx, hy, 0.04f);
    if (r || render_reset_page() != RESET_PG_HOLD || render_reset_hold_frac() != 0) FR_FAIL("slid off HOLD TO ERASE and back, held 8 s: answered %d (that press was done)", r);
    render_reset_hold(false, 0, 0, 0, 0, 0.04f);
    /* a press that began on NO, slid onto the button and held: nothing */
    r = 0; for (k = 0; k < HOLD_N(8.0f); k++) r |= render_reset_hold(true, nx, ny, hx, hy, 0.04f);
    if (r) FR_FAIL("pressed NO, slid onto HOLD TO ERASE, held 8 s: answered %d", r);
    render_reset_hold(false, 0, 0, 0, 0, 0.04f);
    /* the full hold: the answer, by the box */
    r = 0; for (k = 0; k < HOLD_N(RESET_HOLD_S + 0.2f) && !r; k++) r = render_reset_hold(true, hx, hy, hx, hy, 0.04f);
    if (r != RESET_ANS_ERASE || render_reset_page() != RESET_PG_NONE) FR_FAIL("the full hold, the box unticked: answer %d (want %d), page %d", r, RESET_ANS_ERASE, render_reset_page());
    if (k < HOLD_N(RESET_HOLD_S - 0.1f)) FR_FAIL("the hold answered after %.2f s (want %.1f)", k * 0.04f, RESET_HOLD_S);
    render_reset_open(RESET_PG_CHOOSE); tap_btn(RESET_BTN_ERASE); tap_btn(RESET_BTN_WIFI); tap_btn(RESET_BTN_NEXT);
    r = 0; for (k = 0; k < HOLD_N(RESET_HOLD_S + 0.2f) && !r; k++) r = render_reset_hold(true, hx, hy, hx, hy, 0.04f);
    if (r != RESET_ANS_ERASE_WIFI) FR_FAIL("the full hold, the box ticked: answer %d (want %d)", r, RESET_ANS_ERASE_WIFI);
    /* the prompt down: nothing answers */
    if (render_reset_hold(true, hx, hy, hx, hy, 60) || render_reset_tap(hx, hy, hx, hy) || render_reset_hit(hx, hy)) FR_FAIL("the prompt closed still answers");
    return 0;
}

/* ---- the board ---- */
static int s_committed;                               /* the platform's goodbyes (main.c: the link down): only once the erase is certain */
static void on_committed(void) { s_committed++; }
static int reset_this_tank(void) {
    if (!rich_board()) FR_FAIL("could not stage a used board");
    const uint32_t old = T.uid;
    { const char *fw = fw_source_wrong(); if (fw) FR_FAIL("the boards' source: %s", fw); }
    dev_stage();
    progression_reset(&T, 41);
    for (int pass = 0; pass < 2; pass++) {
        const char *when = pass ? "after a boot" : "at once";
        { const char *dw = dev_wrong(); if (dw) FR_FAIL("RESET THIS TANK (%s): %s", when, dw); }
        if (T.sd_balance != W0) FR_FAIL("RESET THIS TANK (%s): the wallet is %d (want %d)", when, (int)T.sd_balance, W0);
        if (shelf_count() != SHELF0) FR_FAIL("RESET THIS TANK (%s): %d tanks on the shelf (want %d)", when, shelf_count(), SHELF0);
        if (T.sd_receipts != RCPT0) FR_FAIL("RESET THIS TANK (%s): the receipts are %#x (want %#x)", when, T.sd_receipts, RCPT0);
        if (!(T.sd_unlocks & SD_ITEM_BREED)) FR_FAIL("RESET THIS TANK (%s): SELECTIVE BREEDING, bought for every tank, is gone", when);
        if (!shelf_type_paid(SHELF_TYPE_PIRANHA)) FR_FAIL("RESET THIS TANK (%s): the piranha tank, paid for once for the board, no longer reads paid for", when);
        if (T.sd_unlocks & (SD_ITEM_CASTLE | SD_ITEM_SNAIL)) FR_FAIL("RESET THIS TANK (%s): the old tank's castle or snail is still there", when);
        if (T.uid == old || T.n_fish != 2 || T.fish[0].stage != STAGE_FRY) FR_FAIL("RESET THIS TANK (%s): not a fresh tank of two fry", when);
        if (collection_count() != 0) FR_FAIL("RESET THIS TANK (%s): the old tank's collection is still there", when);
        if (!progression_setup_pending()) FR_FAIL("RESET THIS TANK (%s): the welcome is not owed", when);
        if (!has_wifi()) FR_FAIL("RESET THIS TANK (%s): the Wi-Fi was forgotten", when);
        if (has_frame("reset") || has_frame("wipe")) FR_FAIL("RESET THIS TANK (%s): a journal left", when);
        if (!pass) boot();
    }
    return 0;
}
static int erase_everything(void) {
    for (int wifi = 0; wifi < 2; wifi++) {
        if (!rich_board()) FR_FAIL("could not stage a used board");
        if (!is_old()) FR_FAIL("the staged board is not as staged");
        touch_fit_t fit = { 1.05f, -7.0f, 0.97f, 4.0f }; adv_init(ADV_CELL_STOCK, &fit);
        touch_cal_t cal0 = *adv_touch_cal();
        char code0[NET_CODE_LEN + 1] = "", code1[NET_CODE_LEN + 1] = "";
        if (!net_port_board_code(code0)) FR_FAIL("no board code");
        T.screen_turned = false; T.orient_lock = true; T.orient_q = (uint8_t)TANK_QUARTER(2);
        const uint32_t old = T.uid;
        s_committed = 0;
        dev_stage();
        if (!progression_erase_all(&T, 99, wifi, on_committed)) FR_FAIL("ERASE EVERYTHING was refused on a board that takes writes");
        if (s_committed != 1) FR_FAIL("ERASE EVERYTHING: the platform's goodbye was called %d times (want once)", s_committed);
        for (int pass = 0; pass < 3; pass++) {
            const char *w = new_wrong(wifi);
            if (w) FR_FAIL("ERASE EVERYTHING (%s, %s): %s", wifi ? "the box ticked" : "the box unticked", pass ? "after a boot" : "at once", w);
            if (T.uid == old || !T.uid) FR_FAIL("ERASE EVERYTHING: the tank's ID is the old one's, or none");
            if (memcmp(adv_touch_cal(), &cal0, sizeof cal0)) FR_FAIL("ERASE EVERYTHING: the touch map changed");
            if (!net_port_board_code(code1) || strcmp(code0, code1)) FR_FAIL("ERASE EVERYTHING: the board code changed (%s -> %s)", code0, code1);
            if (!net_port_crew()) FR_FAIL("ERASE EVERYTHING: HATCHERY CREW is gone");
            { const char *dw = dev_wrong(); if (dw) FR_FAIL("ERASE EVERYTHING (%s): %s", pass ? "after a boot" : "at once", dw); }
            if (!T.orient_lock || T.orient_q != (uint8_t)TANK_QUARTER(2)) FR_FAIL("ERASE EVERYTHING: the way up (ROTATION locked) was not kept");
            const uint32_t uid = T.uid;
            boot();
            if (progression_erase_settled_take()) FR_FAIL("ERASE EVERYTHING, finished at once: the boot after says it finished it");
            if (T.uid != uid) FR_FAIL("ERASE EVERYTHING: the boot after came up with another tank (%08x, was %08x)", (unsigned)T.uid, (unsigned)uid);
        }
        /* and it is a board that works: a tank bought into, saved, there after a boot */
        progression_setup_done(&T); T.sd_balance = 55; progression_save(&T); boot();
        if (T.sd_balance != 55 || progression_setup_pending()) FR_FAIL("after ERASE EVERYTHING the new tank did not keep what it earned");
    }
    return 0;
}
/* AN HONEST ERASE (shelf.c shelf_erase_all): afterwards every byte of the shelf's area reads blank - what carries no mark
   of the shelf's too: a removed tank's snapshot (forget_slot erases only the header's sector), a visitor book's payload
   with no head (a write cut between them), whatever the flash held before the tank. A blank area erases nothing; the
   slots that carry a mark go FIRST (a cut leaves the least) */
extern int g_sim_shelf_erased;
static bool slot_blank(int k) {
    static uint8_t b[SHELF_SLOT_SIZE];
    if (!shelf_port_read((uint32_t)k * SHELF_SLOT_SIZE, b, sizeof b)) return false;
    for (size_t i = 0; i < sizeof b; i++) if (b[i] != 0xff) return false;
    return true;
}
static int erase_blank(void) {
    const int slots = (int)(shelf_port_size() / SHELF_SLOT_SIZE) < SHELF_SLOTS_MAX ? (int)(shelf_port_size() / SHELF_SLOT_SIZE) : SHELF_SLOTS_MAX;
    /* a blank shelf: reads alone */
    board_new(); boot(); progression_setup_done(&T); progression_save(&T);
    g_sim_shelf_erased = 0;
    if (!progression_erase_all(&T, 7, false, NULL)) FR_FAIL("a new board: the erase was refused");
    if (g_sim_shelf_erased) FR_FAIL("an empty, blank shelf: %d sectors erased (want none)", g_sim_shelf_erased);
    /* ... and again on the board just erased: still none (the 13 s is paid once) */
    boot(); g_sim_shelf_erased = 0;
    if (!progression_erase_all(&T, 8, false, NULL) || g_sim_shelf_erased) FR_FAIL("a board erased once, erased again: %d sectors erased (want none)", g_sim_shelf_erased);
    /* THE REAL LEFTOVERS, none with a mark: a tank removed (its snapshot and blobs stay past the header's sector), a visitor
       book's payload with no head, bytes in the area's tail past the last slot */
    if (!rich_board()) FR_FAIL("could not stage a used board");
    {   int with_save = -1; for (int i = 0; i < shelf_count(); i++) if (shelf_get(i)->save_len) with_save = i;
        if (with_save < 0 || !shelf_get(with_save)->thumb_w) FR_FAIL("the staging: no shelved tank with a save and a snapshot");
        const int marked0 = storage_marked(); const long bytes0 = area_nonblank();
        if (!shelf_remove(with_save)) FR_FAIL("the staging: REMOVE refused");
        if (storage_marked() != marked0 - 1 || area_nonblank() <= 0 || area_nonblank() >= bytes0) FR_FAIL("the staging: REMOVE did not leave the tank's snapshot behind its erased header (%ld bytes, was %ld)", area_nonblank(), bytes0);
        static uint8_t payload[1024]; for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 3 + 5);
        const int free_k = slots - 2;                 /* (a slot nothing uses: the staged board fills from the front) */
        if (!slot_blank(free_k)) FR_FAIL("the staging: slot %d is not free", free_k);
        shelf_port_write((uint32_t)free_k * SHELF_SLOT_SIZE + SHELF_BOOK_HDR, payload, sizeof payload);   /* book_put's payload, its head never written */
        shelf_port_write((uint32_t)slots * SHELF_SLOT_SIZE + 512, payload, 64);                             /* the tail past the last slot */
        boot();
        if (!progression_erase_all(&T, 9, false, NULL)) FR_FAIL("leftovers: the erase was refused");
        if (area_nonblank() != 0) FR_FAIL("after ERASE EVERYTHING %ld bytes of the shelf's area do not read blank (a removed tank's snapshot, a headless book's payload, the tail)", area_nonblank());
        boot();
        const char *w = new_wrong(false); if (w) FR_FAIL("leftovers erased, the boot after: %s", w);
    }
    /* a shelf full of what the flash held before the tank, a real tank among it: everything goes (once), the tank FIRST */
    board_new(); boot(); progression_setup_done(&T); progression_save(&T);
    {   static uint8_t junk[4096];
        for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(0x5a ^ (i * 7));
        for (uint32_t o = 0; o + 4096 <= shelf_port_size(); o += 4096) shelf_port_write(o, junk, sizeof junk);
        boot();
        if (shelf_count() || shelf_book_count()) FR_FAIL("the staging: bytes that are not the shelf's read as %d tanks, %d books", shelf_count(), shelf_book_count());
        if (shelf_new(SHELF_TYPE_FISH, clock_port_now_unix()) < 0) FR_FAIL("the staging: no new tank on a shelf of foreign bytes");
        int tk = -1; uint8_t m[4];
        for (int k = 0; k < slots; k++) if (shelf_port_read((uint32_t)k * SHELF_SLOT_SIZE, m, 4) && (m[0] | m[1] << 8 | m[2] << 16 | (uint32_t)m[3] << 24) == 0x31535450u) tk = k;
        if (tk < 0) FR_FAIL("the staging: the new tank's slot not found");
        char pre[500]; snprintf(pre, sizeof pre, "%s.pre", s_dir); dir_copy(s_dir, pre);
        /* the power gone after the journal and ONE sector of the shelf's erase: it was the tank's header, not slot 0's old bytes */
        boot(); shelf_port_sim_cut(2); progression_erase_all(&T, 9, false, NULL); shelf_port_sim_cut(-1);
        if (!shelf_port_read((uint32_t)tk * SHELF_SLOT_SIZE, m, 4) || (m[0] & m[1] & m[2] & m[3]) != 0xff) FR_FAIL("cut after one sector of the erase: the real tank's header (slot %d) was not the first thing erased", tk);
        if (tk != 0 && slot_blank(0)) FR_FAIL("cut after one sector of the erase: slot 0's old bytes went before the tank");
        boot(); boot();                               /* the boot finishes the sweep */
        if (area_nonblank() != 0) FR_FAIL("a cut in the erase, then a boot: %ld bytes of the area still not blank (the sweep did not resume)", area_nonblank());
        { const char *w = new_wrong(false); if (w && strcmp(w, "LIGHTS OUT's setting was not kept")) FR_FAIL("a cut in the erase, then boots: %s", w); }
        /* uncut: every sector of the area erased, once */
        dir_copy(pre, s_dir); boot(); g_sim_shelf_erased = 0;
        if (!progression_erase_all(&T, 9, false, NULL)) FR_FAIL("a shelf of foreign bytes: the erase was refused");
        if (area_nonblank() != 0) FR_FAIL("a shelf of foreign bytes: %ld bytes left after the erase", area_nonblank());
        const int want = (int)(shelf_port_size() / 4096) - (SHELF_SLOT_SIZE / 4096 - 1);   /* (the new tank's slot was erased for it: only its header's sector holds anything) */
        if (g_sim_shelf_erased != want) FR_FAIL("a shelf of foreign bytes: %d sectors erased (want every one that held anything, once: %d)", g_sim_shelf_erased, want);
        boot();
        if (shelf_free_places() != slots - 1) FR_FAIL("after it %d places are free (want %d)", shelf_free_places(), slots - 1);
        { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd); }
    }
    /* slots a write was cut in: each erased whole */
    board_new(); boot(); progression_setup_done(&T); progression_save(&T);
    {   uint8_t save[64]; size_t got = 0; uint32_t m;
        if (!persist_port_load(save, sizeof save, &got) || !progression_blob_is_save(save, sizeof save)) FR_FAIL("the staging: no save to take a head from");
        static uint8_t pic[8192]; for (size_t i = 0; i < sizeof pic; i++) pic[i] = (uint8_t)(i * 13 + 1);
        const int A = 3, B = 40, C = slots - 1, D = 77;
        shelf_port_write((uint32_t)A * SHELF_SLOT_SIZE + SHELF_THUMB_OFF, pic, sizeof pic);          /* A: the snapshot and the SAVE landed, the header (last) did not */
        shelf_port_write((uint32_t)A * SHELF_SLOT_SIZE + SHELF_SAVE_OFF, save, sizeof save);
        m = 0x31585450u; shelf_port_write((uint32_t)B * SHELF_SLOT_SIZE + SHELF_THUMB_OFF, pic, sizeof pic);   /* B: the snapshot and the BLOBS landed */
        shelf_port_write((uint32_t)B * SHELF_SLOT_SIZE + SHELF_EXTRA_OFF, &m, 4);
        m = 0x31535450u; shelf_port_write((uint32_t)C * SHELF_SLOT_SIZE, &m, 4);                     /* C: a header that is no tank any more */
        shelf_port_write((uint32_t)D * SHELF_SLOT_SIZE + SHELF_THUMB_OFF, pic, sizeof pic);          /* D: only the snapshot landed (no mark at all) */
        boot();
        if (!progression_erase_all(&T, 7, false, NULL)) FR_FAIL("half-written slots: the erase was refused");
        if (!slot_blank(A)) FR_FAIL("a slot with a save and no header (a write cut before the header) was not erased whole");
        if (!slot_blank(B)) FR_FAIL("a slot with a tank's blobs and no header (a write cut after them) was not erased whole");
        if (!slot_blank(C)) FR_FAIL("a slot with a stale tank header was not erased whole");
        if (!slot_blank(D)) FR_FAIL("a slot holding only a snapshot (a write cut right after it) was not erased");
        if (area_nonblank() != 0) FR_FAIL("half-written slots: %ld bytes left", area_nonblank());
    }
    return 0;
}
static int erase_refused(void) {
    if (!rich_board()) FR_FAIL("could not stage a used board");
    const uint32_t old = T.uid;
    shelf_port_sim_fail(0); s_committed = 0;
    bool did = progression_erase_all(&T, 99, true, on_committed);
    shelf_port_sim_fail(-1);
    if (did) FR_FAIL("the first write refused: ERASE EVERYTHING said it went ahead");
    if (s_committed) FR_FAIL("the first write refused: the platform's goodbye (the link down) was called all the same");
    if (T.uid != old || !is_old()) FR_FAIL("the first write refused: the board in RAM changed");
    boot();
    if (T.uid != old || !is_old()) FR_FAIL("the first write refused: the board changed");
    return 0;
}
static int erase_cut(void) {
    char pre[500]; snprintf(pre, sizeof pre, "%s.pre", s_dir);
    int runs = 0, used = 0;
    for (int wifi = 0; wifi < 2; wifi++) {
        if (!rich_board()) FR_FAIL("could not stage a used board");
        const uint32_t old = T.uid;
        dir_copy(s_dir, pre);
        shelf_port_sim_cut(1000); progression_erase_all(&T, 99, wifi, on_committed); used = 1000 - shelf_port_sim_cut(-1);
        if (used < 8) FR_FAIL("the erase made only %d writes - the cut is not counting them", used);
        for (int n = 0; n <= used; n++)
            for (int m = 0; m <= 40; m++) {
                dir_copy(pre, s_dir); boot();
                if (!is_old() || T.uid != old) FR_FAIL("cut %d/%d: the staged board did not come back", n, m);
                shelf_port_sim_cut(n); bool did = progression_erase_all(&T, 99, wifi, on_committed); shelf_port_sim_cut(-1);   /* the power gone at write n */
                shelf_port_sim_cut(m); boot(); int left = shelf_port_sim_cut(-1);                                /* the boot after, cut at its write m */
                const bool said = progression_erase_settled_take();
                if (said != (n > 0 && n < used)) FR_FAIL("cut %d/%d: the boot after %s it found the erase to finish (the platform's own clean-up hangs on it)", n, m, said ? "said" : "did not say");
                if (progression_erase_settled_take()) FR_FAIL("cut %d/%d: the boot said twice that it finished the erase", n, m);
                boot(); runs++;                                                                                  /* ... and a clean one */
                if (n == 0) {
                    if (did) FR_FAIL("cut 0: the journal never landed and ERASE EVERYTHING said it went ahead");
                    if (!is_old() || T.uid != old) FR_FAIL("cut 0/%d: not even the journal landed, and the board is not the old one whole (wallet %d, shelf %d, collection %d)", m,
                                                           (int)T.sd_balance, shelf_count(), collection_count());
                } else {
                    const char *w = new_wrong(wifi);
                    if (w) FR_FAIL("cut %d/%d of %d (%s): %s (wallet %d, shelf %d, collection %d, unlocks %#x)", n, m, used, wifi ? "box ticked" : "box unticked", w,
                                   (int)T.sd_balance, shelf_count(), collection_count(), (unsigned)T.sd_unlocks);
                    const uint32_t uid = T.uid; boot();
                    if (T.uid != uid || new_wrong(wifi)) FR_FAIL("cut %d/%d: the new board did not stay as it was at the next boot", n, m);
                }
                if (left > 0) break;                  /* (the boot made fewer writes than m: every one of its writes has been cut) */
            }
    }
    { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd); }
    if (!fails) printf("selftest-full-reset: the power cut - %d writes of the erase, each cut, and each write of the boot after (%d runs): the old board whole or the new one whole\n", used, runs);
    return 0;
}

/* A WRITE REFUSED, the board running on (not a power cut: the writes after it land). THE RULE (progression.c): the journal
   goes only when everything is done; what is owed is tried again at every save and every boot; the tank runs meanwhile */
static const char *journal_wrong(void) {              /* the journal gone with something of the old board still there */
    if (has_frame("wipe")) return NULL;
    if (storage_marked()) return "the journal is gone and a tank or a book is still on the flash";
    if (area_nonblank()) return "the journal is gone and the shelf's area does not read blank";
    if (shelf_count() || shelf_book_count()) return "the journal is gone and the shelf still lists a tank or a book";
    if (has_rescue()) return "the journal is gone and the rescue copy is still there";
    return NULL;
}
static int erase_refusals(void) {
    int used = 0, runs = 0;
    for (int wifi = 0; wifi < 2; wifi++) {
        if (!rich_board()) FR_FAIL("could not stage a used board");
        char pre[500]; snprintf(pre, sizeof pre, "%s.pre", s_dir); dir_copy(s_dir, pre);
        shelf_port_sim_cut(1000); progression_erase_all(&T, 99, wifi, NULL); used = 1000 - shelf_port_sim_cut(-1);
        /* ONE write refused, each in turn: the erase, then a save (the retry), then a boot */
        for (int n = 0; n <= used; n++) {
            dir_copy(pre, s_dir); boot();
            shelf_port_sim_fail(n); bool did = progression_erase_all(&T, 99, wifi, NULL); shelf_port_sim_fail(-1);
            const char *jw = did ? journal_wrong() : NULL; if (jw) FR_FAIL("write %d refused: %s", n, jw);
            if (did && shelf_count() + shelf_book_count() != storage_marked()) FR_FAIL("write %d refused: the shelf lists %d tanks and %d books, the flash holds %d (ghosts)", n, shelf_count(), shelf_book_count(), storage_marked());
            if (did) progression_save(&T);
            if (did && (jw = journal_wrong())) FR_FAIL("write %d refused, after the next save: %s", n, jw);
            boot(); runs++;
            if (n == 0) { if (did || !is_old()) FR_FAIL("the first write refused: the board is not the old one whole"); continue; }
            const char *w = new_wrong(wifi);
            if (w) FR_FAIL("write %d of %d refused (%s), a save and a boot later: %s (shelf %d, on the flash %d)", n, used, wifi ? "box ticked" : "box unticked", w, shelf_count(), storage_marked());
        }
        /* S1: refused from write n ON (a slot's erase that will not go), refused again through the boot after; the first
           save after that boot must not let the journal go with a tank left; a boot once the flash takes writes finishes */
        for (int n = 1; n <= used; n++) {
            dir_copy(pre, s_dir); boot();
            shelf_port_sim_fail_n(n, 100000); progression_erase_all(&T, 99, wifi, NULL);
            boot();                                                   /* (still refusing) */
            shelf_port_sim_fail(-1);
            const char *jw = journal_wrong(); if (jw) FR_FAIL("refused from write %d on, and through a boot: %s", n, jw);
            progression_save(&T);                                     /* the first save once the flash takes writes */
            if ((jw = journal_wrong())) FR_FAIL("refused from write %d on, the first save after the boot: %s", n, jw);
            boot(); progression_save(&T); boot(); runs++;
            const char *w = new_wrong(wifi);
            if (w) FR_FAIL("refused from write %d on (%s), then the flash well again: %s (shelf %d, on the flash %d, journal %s)", n, wifi ? "box ticked" : "box unticked", w,
                           shelf_count(), storage_marked(), has_frame("wipe") ? "there" : "gone");
        }
        { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", pre); (void)system(cmd); }
    }
    /* S2: a tank REMOVED whose erase the flash refused - gone from the list, still on the flash. ERASE EVERYTHING goes by the flash */
    if (!rich_board()) FR_FAIL("could not stage a used board");
    {   const int on_flash = storage_marked();
        shelf_port_sim_fail(0); shelf_remove(0); shelf_port_sim_fail(-1);
        if (shelf_count() != SHELF0 - 1 || storage_marked() != on_flash) FR_FAIL("S2's staging: a removed tank's refused erase did not leave it on the flash (list %d, flash %d of %d)", shelf_count(), storage_marked(), on_flash);
        if (!progression_erase_all(&T, 99, false, NULL)) FR_FAIL("S2: the erase was refused");
        if (storage_marked()) FR_FAIL("S2: a tank the list had dropped is still on the flash after ERASE EVERYTHING (%d slots)", storage_marked());
        boot();
        const char *w = new_wrong(false); if (w) FR_FAIL("S2: the boot after: %s (shelf %d)", w, shelf_count());
    }
    /* the Wi-Fi's store refuses the forget: the ticked box is not let go - the journal stands, the next save or boot forgets it */
    for (int how = 0; how < 2; how++) {
        if (!rich_board()) FR_FAIL("could not stage a used board");
        net_sim_forget_refuse(1);
        progression_erase_all(&T, 99, true, NULL);
        if (!has_wifi()) FR_FAIL("the Wi-Fi forget's staging: it was not refused");
        if (!has_frame("wipe")) FR_FAIL("the Wi-Fi forget refused: the journal went all the same - the ticked box was ignored for good");
        if (how) progression_save(&T); else { boot(); if (!progression_erase_settled_take()) FR_FAIL("the Wi-Fi forget refused: the boot after did not say it had the erase to finish"); progression_save(&T); }
        if (has_wifi()) FR_FAIL("the Wi-Fi forget refused once: still there after %s", how ? "the next save" : "the next boot");
        boot();
        const char *w = new_wrong(true); if (w) FR_FAIL("the Wi-Fi forget refused once, %s later: %s", how ? "a save" : "a boot", w);
    }
    net_sim_forget_refuse(0);
    if (!fails) printf("selftest-full-reset: a write refused - each of the erase's %d writes alone, each onward through a boot, a removed tank the list had dropped, "
                       "the Wi-Fi store (%d runs): the journal stands until the board is new\n", used, runs);
    return 0;
}

int selftest_full_reset(void) {
    board_env();
    if (pages_drawn() || pages_baked() || page_erasing() || pages_work()) return 1;
    if (reset_this_tank() || erase_everything() || erase_blank() || erase_refused() || erase_refusals() || erase_cut()) return 1;
    { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", s_dir); (void)system(cmd); }
    if (touch_port_reset_prompt()) fails++;
    if (fails) { printf("selftest-full-reset: FAILED (%d)\n", fails); return 1; }
    printf("selftest-full-reset (%dx%d): PASS - the words fit and every button answers where it is drawn; RESET THIS TANK keeps the wallet, the shelf and the "
           "receipts; ERASE EVERYTHING leaves a new board (the touch map, the board code, the crew kept; the Wi-Fi by its box) and only by the full hold; "
           "a power cut ends in the old board or the new one\n", TANK_W, TANK_H);
    return 0;
}

/* --reset-shot <prefix> (0.4.2): the prompt's pages over a living tank, for the owner's approval: <prefix>_<name>.ppm */
static void fr_ppm(const char *prefix, const char *name, const uint16_t *fb) {
    char path[700]; snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TANK_W, TANK_H);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t v = FB_SWAP16(fb[i]);
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}
int full_reset_shot(const char *prefix) {
    board_env(); board_new(); boot(); progression_setup_done(&T);
    static uint16_t scene[TANK_W * TANK_H];
    render_set_scene_cache(scene);
    for (int i = 0; i < 600; i++) tank_tick(&T, 1.0f / 30.0f, advisor_rules);
    static const struct { int pg, wifi; float hold; const char *name; } S[8] = {
        { RESET_PG_CHOOSE, 0, 0, "1_choose" }, { RESET_PG_TANK, 0, 0, "2_reset_this_tank" }, { RESET_PG_ERASE, 0, 0, "3_erase_unticked" },
        { RESET_PG_ERASE, 1, 0, "4_erase_ticked" }, { RESET_PG_HOLD, 0, 0.5f, "5_hold_half" }, { RESET_PG_HOLD, 1, 0, "6_hold_wifi_start" }, { RESET_PG_REFUSED, 0, 0, "7_could_not_erase" }, { RESET_PG_ERASING, 0, 0, "8_erasing" } };
    for (int k = 0; k < 8; k++) {
        render_reset_open(S[k].pg); render_reset_set_wifi(S[k].wifi); render_reset_set_hold(S[k].hold);
        render_frame_begin(); render_tank(&T, fa, TANK_W); render_reset_prompt(fa, TANK_W, 0.7f);
        fr_ppm(prefix, S[k].name, fa);
    }
    render_reset_close();
    { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", s_dir); (void)system(cmd); }
    printf("reset-shot: wrote %s_*.ppm\n", prefix);
    return 0;
}

/* --reset-bench (0.4.2): what a frame of each of the prompt's pages costs to draw, on the host - the pages against each
   other (RESET THIS TANK? is the old single prompt's panel, 20 px taller), drawn whole and from the bake */
#include <time.h>
static double fr_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
int full_reset_bench(void) {
    static uint16_t scene[TANK_W * TANK_H];
    board_env(); board_new(); boot(); progression_setup_done(&T);
    render_set_scene_cache(scene);
    render_frame_begin(); render_tank(&T, fa, TANK_W);
    const int N = 3000;
    for (int baked = 0; baked < 2; baked++) {
        render_set_modal_alloc(baked ? malloc : NULL, baked ? free : NULL);
        for (int pg = RESET_PG_CHOOSE; pg <= RESET_PG_ERASING; pg++) {
            render_reset_open(pg); if (pg == RESET_PG_HOLD) render_reset_set_hold(0.5f);
            double t0 = fr_now();
            for (int i = 0; i < N; i++) { render_frame_begin(); render_reset_prompt(fa, TANK_W, 0.5f); }
            printf("reset-bench: %-18s %s  %.1f us a frame\n", PG_NAME[pg], baked ? "baked" : "whole", (fr_now() - t0) * 1000.0 / N);
        }
    }
    render_reset_close(); render_set_modal_alloc(NULL, NULL);
    { char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", s_dir); (void)system(cmd); }
    return 0;
}
