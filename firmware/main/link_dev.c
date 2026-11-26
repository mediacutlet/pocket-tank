/* link_dev.c — the tank link on the director, before it has a page or doors
 * (docs/tank-link-0.4.md: LINK waits for 0.3.2's settings rework; the doors
 * are phase 4). It runs the real engine over the real radio with GHOST FISH:
 * passports that are not drawn and not in the tank - they cross, visit, are
 * leased, poof and come home exactly as fish will, and every step is logged,
 * so two boards on the desk can prove the protocol before the tank takes part.
 *
 *   link on | off                 the radio + link mode (off: everyone home, doors closed, radio down)
 *   link die                      the radio just stops, no goodbye (a sudden death, for the tests)
 *   link arm left|right           the hold on an arrow (both tanks, a few s apart)
 *   link close left|right         the keeper closes a door
 *   link ghost left|right         a ghost of ours swims out of that door
 *   link visit <fish> left|right  one of OUR real fish heads for that door (the model may change its mind)
 *   link home                     every visitor here heads home
 *   link drift on|off|imu         staged drifting, or the IMU (the default)
 *   link status                   badge, doors, ghosts, the radio's counts
 *   link fry [home|ours]          a cross-tank fry now: the first visitor here courts our best (the coin forced, or not)
 *   link give <fish> [left|right] GIVE, confirmed (phase 6): offer one of our fish to the tank next door for good
 *   link modal <fish> | off       the GIVE modal for that fish, as its GIVE box opens it (staging, frame rate)
 *   link accept | refuse          our keeper's answer to a gift offered to us (the card's ACCEPT / NO THANKS)
 *   link gifts                    the gift notes (GIVEN / ARRIVING / RECEIVED) and what is in flight
 *   link book                     the tank's own collection and gift notes (they move with it on MY TANKS)
 * The engine and the ghosts live in PSRAM; the radio takes its 16 KB of
 * internal RAM only between `link on` and `link off`. */
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "link.h"
#include "tank_link.h"
#include "progression.h"
#include "link_dev.h"
#include "touch_port.h"
#include "gift.h"
#include "passport.h"
#include "collection.h"
#include "shelf.h"
#include "link_port_espnow.h"
#include "imu_port.h"
#include "render.h"
#include "setup.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "link";
#define GHOSTS 2
#define GOODBYE_WAIT_MS 400
static link_t *L;
static bool s_on;                              /* the radio is up and the engine runs */
static bool s_page;                            /* link mode's page is up (settings -> LINK) */
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
#include "radio_hosted.h"
#include "esp_system.h"
#endif
#ifdef TANK_P4
#include "link_note.h"
/* (0.4.3) LINK's cards on the big screens: STARTING THE LINK while the radio task starts the C6 (the tank task never
   waits on it - the 11-24 audit), THE LINK DID NOT START when it could not (Strato 11-23). The logic: link_note.h */
static link_note_t s_ln = { LN_NONE, -1, false, false };
static bool s_ln_esc;                           /* (the restart chain) the LN_WAIT card is its own: STARTING LINK (render_link_escape) */
static int s_ho_pct;                           /* UPDATING YOUR DEVICE's bar (the one install's handover; 0 off the radio image) */
_Static_assert((int)LN_BEGIN_WAIT == (int)LP_BEGIN_WAIT && (int)LN_BEGIN_UP == (int)LP_BEGIN_UP && (int)LN_BEGIN_FAIL == (int)LP_BEGIN_FAIL, "link_note.h's start answers are the port's");
_Static_assert((int)LN_TAP_OK == (int)LNOTE_TAP_OK && (int)LN_TAP_OUTSIDE == (int)LNOTE_TAP_OUTSIDE && (int)LN_TAP_NONE == (int)LNOTE_TAP_NONE, "link_note.h's taps are render.h's");
#endif
static uint32_t s_page_quiet;
static uint32_t s_sd_peer;                     /* the tank at the door the sand dollar dial was opened for (0.4.2) */
static bool     s_sd_dial;                     /* ... SEND comes from the dial (false: the director's `link sd <edge> <n>`, any tank there) */
static uint32_t s_fry_at;                      /* `link fry`: born when its home's room is known (or at this time) */                  /* the last touch on the page, or the last door to open */
#define LINK_PAGE_DONE_MS 20000                /* linked and left alone this long: back to the tank by itself (2026-10-05, a keeper who never finds DONE) */
static bool s_dir;                             /* the director's `link on`: the radio stays up until `link off` */
static int s_drift = -1;                       /* -1 = the IMU, 0 / 1 = staged */
static uint32_t s_last_log;
typedef enum { G_HOME, G_OFFERED, G_AWAY } gst_t;
typedef struct {                               /* PSRAM with the engine: internal RAM is the bowl's tightest budget */
    struct { gst_t st; passport_t pp; uint32_t left_at; } mine[GHOSTS];
} ghosts_t;
static ghosts_t *G;
static tank_t *s_tank;                         /* the tank the link serves (visitors swim in it: tank_link.c) */
#define s_mine  (G->mine)

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static const char *EDGE[2] = { "left", "right" };
static const char *BADGE[LINK_BADGES] = { "circle", "triangle", "square", "diamond" };
void device_dirty_mask_for_radio(bool radio);   /* main.c: the 1.8's dirty mask steps out of internal RAM while linked */
static bool radio_start(void) { device_dirty_mask_for_radio(true); if (link_port_start()) return true; device_dirty_mask_for_radio(false); return false; }
#ifdef TANK_P4
static int radio_begin(void) { device_dirty_mask_for_radio(true); int r = link_port_begin(); if (r == LP_BEGIN_FAIL) device_dirty_mask_for_radio(false); return r; }
static void begun(int r);                      /* below: the start's answer, on the tank task */
#endif
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
static void ho_poll(tank_t *t);                /* below: the radio chip's update (the one install) */
static void ho_act(tank_t *t, int a);
#endif
static void radio_stop(void) { link_port_stop(); device_dirty_mask_for_radio(false); }
static const char *WHY[] = { "keeper", "lost", "sleep", "drift", "mismatch" };

static int guests(void) { if (!s_tank) return 0; int n = 0; for (int k = 0; k < N_VISIT_MAX; k++) n += s_tank->vis_used[k]; return n; }
static bool fish_away(void) { if (!s_tank) return false; for (int i = 0; i < s_tank->n_fish; i++) if (s_tank->fish[i].away) return true; return false; }
static const char *name_of(uint32_t uid) {
    if (s_tank) for (int i = 0; i < N_FISH_SLOTS; i++) if (s_tank->fish[i].uid == uid) return s_tank->fish[i].name;
    return "?";
}
static int ghosts_away(void) { int n = 0; for (int i = 0; i < GHOSTS; i++) n += s_mine[i].st == G_AWAY; return n; }
static void radio_down_if_idle(void);          /* below, with link mode's page */
static bool any_door(void);                    /* below */
static void events(void);
static void goodbye(bool tell);                /* below */
static void link_page_begin(tank_t *t);        /* below: LINK's page as always */
static int mine(uint32_t uid) { for (int i = 0; i < GHOSTS; i++) if (s_mine[i].pp.uid == uid) return i; return -1; }

static uint32_t s_me;                          /* the tank ID the engine speaks for */
static bool ensure(const tank_t *t) {
    if (L && s_me != t->uid && !s_on) {            /* another tank on the glass since (a reset's new tank, the director's
                                                      staging): the engine starts over as it - the old one's trips are over */
        ESP_LOGI(TAG, "tank now %08lx (was %08lx): link restarts", (unsigned long)t->uid, (unsigned long)s_me);
        s_tank = (tank_t *)t;
        link_host_t h = tank_link_host(s_tank, link_port_send);
        link_init(L, t->uid, &h, esp_random(), now_ms()); s_me = t->uid;
        for (int i = 0; i < GHOSTS; i++) { s_mine[i].pp.home_tank = t->uid; s_mine[i].st = G_HOME; }
    }
    if (L) return true;
    L = heap_caps_calloc(1, (size_t)link_size(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    G = heap_caps_calloc(1, sizeof *G, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!L || !G) { ESP_LOGE(TAG, "no PSRAM for the engine"); free(L); free(G); L = NULL; G = NULL; return false; }
    s_tank = (tank_t *)t;
    link_host_t h = tank_link_host(s_tank, link_port_send);   /* room and visitors: the tank's (tank_link.c) */
    link_init(L, t->uid, &h, esp_random(), now_ms()); s_me = t->uid;
    for (int i = 0; i < GHOSTS; i++) {
        passport_t *p = &s_mine[i].pp; memset(p, 0, sizeof *p);
        p->uid = esp_random() | 1; p->home_tank = t->uid; p->name[0] = 'g'; p->name[1] = (char)('1' + i);
        p->stage = STAGE_ADULT; p->size = 1; p->hunger = 4; p->energy = 6; p->bold = 0.5f; p->sociable = 0.5f;
        s_mine[i].st = G_HOME;
    }
    return true;
}

static void events(void) {
    link_ev_t e; uint32_t now = now_ms();
    while (link_poll(L, &e)) {
        int g = mine(e.uid);
        if (g >= 0) {                              /* one of the director's ghosts: not in the tank */
            switch (e.type) {
            case LEV_CROSS_OK:
                if (s_mine[g].st == G_OFFERED && link_cross(L, e.uid)) { s_mine[g].st = G_AWAY; s_mine[g].left_at = now; ESP_LOGI(TAG, "ghost %s crossed (%s)", s_mine[g].pp.name, EDGE[e.edge]); }
                else { s_mine[g].st = G_HOME; ESP_LOGW(TAG, "ghost %s: too late to cross", s_mine[g].pp.name); }
                break;
            case LEV_CROSS_NO: if (s_mine[g].st == G_OFFERED) { s_mine[g].st = G_HOME; ESP_LOGI(TAG, "ghost %s: the door is glass for now", s_mine[g].pp.name); } break;
            case LEV_FISH_BACK: case LEV_FISH_RESTORE:
                ESP_LOGI(TAG, "ghost %s HOME %s after %.1f s (meals %ld)", s_mine[g].pp.name, e.type == LEV_FISH_BACK ? "through the door" : "from a bubble cloud",
                         (now - s_mine[g].left_at) / 1000.0f, (long)e.pp.eaten);
                s_mine[g].pp.eaten = e.pp.eaten; s_mine[g].st = G_HOME; break;
            default: break;
            }
            continue;
        }
        const char *who = e.type == LEV_VISITOR_IN ? e.pp.name : name_of(e.uid);
        switch (e.type) {                          /* the log first: the tank's handling may free the slot the name is in */
        case LEV_NEARBY: { link_peer_info_t p[LINK_PEERS_MAX]; int n = link_peers(L, p, LINK_PEERS_MAX);
            ESP_LOGI(TAG, "nearby: %d tank%s", n, n == 1 ? "" : "s");
            for (int i = 0; i < n; i++) ESP_LOGI(TAG, "  %08lx %s rssi %d%s", (unsigned long)p[i].tank, BADGE[p[i].badge % LINK_BADGES], p[i].rssi, p[i].drifting ? " (drifting)" : "");
            break; }
        case LEV_ASKING: ESP_LOGI(TAG, "%s arrow: %s", EDGE[e.edge], e.reason >= 2 ? "two tanks asking - wait" : e.peer ? "a tank asks to join here" : "nobody asking"); break;
        case LEV_LINKED: ESP_LOGI(TAG, "LINKED: %s door open to %08lx", EDGE[e.edge], (unsigned long)e.peer); s_page_quiet = now; break;
        case LEV_UNLINKED: ESP_LOGI(TAG, "UNLINKED: %s door closed (%s)", EDGE[e.edge], e.reason < 5 ? WHY[e.reason] : "?"); break;
        case LEV_CROSS_OK: ESP_LOGI(TAG, "%s: on through the %s door", who, EDGE[e.edge]); break;
        case LEV_CROSS_NO: {
            int fi = -1; if (s_tank) for (int i = 0; i < s_tank->n_fish; i++) if (s_tank->fish[i].uid == e.uid) fi = i;
            if (fi >= 0) { const fish_t *f = &s_tank->fish[fi];
                ESP_LOGI(TAG, "%s: door glass (why %d; %s at x %.0f, facing %d, speed %.0f)", who, e.reason,
                         f->door == DOOR_PASSING ? "PASSING" : f->door == DOOR_ASKED ? "ASKED" : "elsewhere", f->x, f->facing, f->speed); }
            else ESP_LOGI(TAG, "%s: the door is glass for now (why %d)", who, e.reason);
            break; }
        case LEV_VISITOR_IN: ESP_LOGI(TAG, "visitor %s (%08lx) swims in at the %s door", who, (unsigned long)e.uid, EDGE[e.edge]); break;
        case LEV_VISITOR_POOF: ESP_LOGI(TAG, "visitor %s POOF (bubbles)", who); break;
        case LEV_VISITOR_HOMEWARD: ESP_LOGI(TAG, "visitor %s heads home", who); break;
        case LEV_FISH_BACK: ESP_LOGI(TAG, "%s is HOME through the %s door", who, EDGE[e.edge]); break;
        case LEV_FISH_RESTORE: ESP_LOGI(TAG, "%s is HOME from a bubble cloud", who); break;
        case LEV_DRIFT: ESP_LOGI(TAG, "DRIFTED - doors paused (%lu s) | imu %d%s", (unsigned long)(e.ms_left / 1000), imu_port_motion(), s_drift < 0 ? "" : " (staged)"); break;
        case LEV_STEADY: ESP_LOGI(TAG, "steady again - doors open"); break;
        case LEV_FRY_ROOM: ESP_LOGI(TAG, "fry: %s's home %s", who, e.reason ? "has room" : "is full"); break;
        case LEV_FRY_IN:   ESP_LOGI(TAG, "fry: %s, OURS, born at %08lx - home through %s", e.pp.name, (unsigned long)e.peer, EDGE[e.edge]); break;
        case LEV_FRY_OUT:  ESP_LOGI(TAG, "fry: %s goes home through %s, its parent after", e.pp.name, EDGE[e.edge]); break;
        case LEV_FRY_KEEP: ESP_LOGI(TAG, "fry: %s - its home said no: ours", e.pp.name); break;
        case LEV_FRY_LOST: ESP_LOGI(TAG, "fry: no answer from its home - no fry here"); break;
        case LEV_GIVE_ASK:     ESP_LOGI(TAG, "gift: %08lx offers %s (`link accept`/`refuse`)", (unsigned long)e.peer, e.pp.name); break;
        case LEV_GIVE_TAKEN:   ESP_LOGI(TAG, "gift: %s ACCEPTED next door - off it goes", who); break;
        case LEV_GIVE_NO:      ESP_LOGI(TAG, "gift: %s stays (%s)", who, e.reason == LINK_GIVE_NO_REFUSED ? "NO THANKS" : e.reason == LINK_GIVE_NO_FULL ? "their tank is full" : e.reason == LINK_GIVE_NO_ANSWER ? "no answer" : e.reason == LINK_GIVE_NO_CANCEL ? "our keeper took it back" : "cannot go now"); break;
        case LEV_GIVE_ALIVE:   ESP_LOGI(TAG, "gift: %s is OURS - in through the door", e.pp.name); break;
        case LEV_GIVE_CANCEL:  ESP_LOGI(TAG, "gift: %08lx kept its fish", (unsigned long)e.peer); break;
        case LEV_GIVE_CLOSED:  ESP_LOGI(TAG, "gift: delivered"); break;
        case LEV_GIVE_RESTORE: ESP_LOGI(TAG, "gift: %s never arrived: back from our note", e.pp.name); break;
        case LEV_SD_IN:   ESP_LOGI(TAG, "sd: %ld from %08lx (gift %lu) at %s", (long)e.amount, (unsigned long)e.peer, (unsigned long)e.nonce, EDGE[e.edge]); break;
        case LEV_SD_DONE: ESP_LOGI(TAG, "sand dollars: our %ld to %08lx (gift %lu): %s", (long)e.amount, (unsigned long)e.peer, (unsigned long)e.nonce,
                                   e.reason == 1 ? "TAKEN" : e.reason == 2 ? "refused - their tank has too many" : "refused"); break;
        default: break;
        }
        if (s_tank) tank_link_event(s_tank, L, &e);
        if (s_tank) {                              /* a sand dollar gift's news (0.4.2): the HUD's line, the coin's spin */
            int ne, na, nv, nk = tank_link_sd_news(&ne, &na, &nv);
            if (nk) { render_link_sd_news(nk, ne, na, nv, s_tank->clock); s_page_quiet = now;
                      ESP_LOGI(TAG, "sd: news %d (%d at %s), balance %ld", nk, na, ne == LINK_LEFT ? "left" : "right", (long)s_tank->sd_balance); }
        }
        if (s_tank && e.type == LEV_FRY_OUT) {         /* (the parent that should follow it) */
            int p = -1; for (int k = 0; k < N_VISIT_MAX; k++) if (s_tank->vis_used[k] && s_tank->fish[N_FISH_MAX + k].follow_uid == e.uid) p = N_FISH_MAX + k;
            if (p >= 0) ESP_LOGI(TAG, "fry: %s follows it (homeward %d)", s_tank->fish[p].name, s_tank->fish[p].homeward);
            else ESP_LOGW(TAG, "fry: no parent follows (%08lx)", (unsigned long)s_tank->fry_give_parent);
        }
    }
}

void link_dev_poll(tank_t *t) {
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
    ho_poll(t);                                /* the radio chip's update (the one install): before the engine exists, too */
#endif
    if (!L || !G) { tank_link_tick(t, NULL); return; }   /* (no engine yet: only a boot's restore of fish that were away) */
#ifdef TANK_P4
    if (s_ln.pending) { const int r = link_port_begin_poll(); if (r != LP_BEGIN_WAIT) begun(r); }
    if (s_on && !link_port_alive()) {          /* (11-24 audit, finding 5) the bus died under the link: say so now, not at the next LINK */
        ESP_LOGE(TAG, "the radio died - every door closes%s", s_page ? "; LINK says it did not start" : "");
        s_dir = false; ln_died(&s_ln, s_page); goodbye(false); link_mode(L, false);   /* (the engine out of link mode, as a close) */
    }
#endif
    uint32_t now = now_ms();
    if (!s_on) {                               /* the radio is down, but a fish still out comes home on the engine's clock */
        if (ghosts_away() || fish_away()) { link_tick(L, now); events(); }
        tank_link_tick(t, L);
        return;
    }
    if (s_fry_at && (t->fry_home_room >= 0 || (int32_t)(now - s_fry_at) > 0)) {
        ESP_LOGI(TAG, "fry: its home %s - born now", t->fry_home_room == 1 ? "has room" : t->fry_home_room == 0 ? "is full" : "did not answer");
        s_fry_at = 0; progression_force_arrival(t);
    }
    if (s_page && setup_is_sdgift()) { s_page_quiet = now; link_mode(L, true); }   /* the dial is up over link mode: it stays (0.4.2) */
    if (s_page && any_door() && (int32_t)(now - s_page_quiet) > LINK_PAGE_DONE_MS) {
        link_view_t v; link_view(L, &v);
        bool pending = false;
        for (int e = 0; e < LINK_EDGES; e++) pending |= v.edge[e].state == LV_ARMED || v.edge[e].state == LV_ASKED || v.edge[e].state == LV_TWO;
        if (!pending) { ESP_LOGI(TAG, "link mode: idle %d s - closing", LINK_PAGE_DONE_MS / 1000); link_dev_page_close(); if (!s_on) return; }
    }
    if (s_page && !link_in_mode(L)) { ESP_LOGI(TAG, "link mode: 2 min idle"); link_dev_page_close(); if (!s_on) return; }
    link_port_drain(L, now);
    /* moving = a HAND moving the tank: two polls in a row over the threshold (imu_port_handled), not one.
       2026-10-20: a watch lying still on its cable flagged THE TANKS DRIFTED every ~16 s with imu_port_moving -
       one reading over 0.013 g holds "moving" a second, two 250 ms apart outlast LINK_NUDGE_MS. Sliding a
       tank away is many polls; a lone glitch or a tap on the glass is one. */
    /* 2026-11-01 (Strato: "a very light vibration" put the notice up; taps kept the doors paused until they closed):
       the drift is CARRIED motion - three polls in a row well past a knock (imu_port_carried) - and none counts
       while a finger is on the glass or 1.5 s after it lifts (a tap shakes the tank) */
    { float fx, fy; static int64_t touch_us; const int64_t tn = esp_timer_get_time();
      if (touch_port_finger(&fx, &fy)) touch_us = tn;
      const bool tapping = touch_us && tn - touch_us < 1500000;
      link_moving(L, s_drift < 0 ? imu_port_carried() && !tapping : s_drift == 1); }
    link_tick(L, now);
    events();
    {   gift_book_t *gb = gift_of(t); uint32_t was = gb ? gb->leaving_uid : 0;
        tank_link_tick(t, L);                  /* the doors, fish at the glass, fish through it */
        if (was && gb && !gb->leaving_uid) {   /* a gift on its way out: gone (its note) or kept (2026-11-02) */
            const char *nm = name_of(was);
            for (int k = 0; k < gb->n_given; k++) if (gb->given[k].uid == was) nm = gb->given[k].pp.name;
            ESP_LOGI(TAG, "gift: %s %s", nm, gift_given_has(gb, was) ? "LEFT - its GIVEN note saved, GIVEN sent"
                     : "STAYS - the tank next door went quiet or closed its door before it left (its TAKE gets CANCEL)");
        }
    }
    events();
    {   /* a crossing under way, twice a second: where the fish is and which way it goes (2026-10-05: accepted
           fish on the boards turned back - the simulator's didn't) */
        static uint32_t s_pass_log;
        if ((int32_t)(now - s_pass_log) >= 500) {
            s_pass_log = now;
            for (int i = 0; i < N_FISH_SLOTS; i++) {
                if (!tank_swims(t, i)) continue;
                const fish_t *f = &t->fish[i];
                if (f->door != DOOR_PASSING && f->door != DOOR_ASKED) continue;
                float gx = f->door_edge == LINK_LEFT ? tank_glass_x0(DOOR_Y) : tank_glass_x1(DOOR_Y);
                ESP_LOGI(TAG, "  %s %s: x %.0f (%.0f) y %.0f f %d yaw %.2f hd %.2f v %.0f->%.0f goal %d hes %.1f",
                         f->name, f->door == DOOR_PASSING ? "PASSING" : "ASKED", f->x, gx, f->y, f->facing, f->yaw, f->heading,
                         f->speed, f->target_speed, f->goal.id, f->hesitate);
            }
        }
    }
    radio_down_if_idle();
    if (!s_on) return;
    if ((int32_t)(now - s_last_log) >= 10000) { s_last_log = now; link_dev_status(); }
}

void link_dev_status(void) {
    if (!L) { ESP_LOGI(TAG, "link: off (never on since boot)"); return; }
    if (s_tank) {                                  /* who is where: ours next door, visitors here */
        char line[320]; int n = 0;
        for (int i = 0; i < s_tank->n_fish && n < 140; i++) if (s_tank->fish[i].away) n += snprintf(line + n, sizeof line - n, "%s%s", n ? ", " : "", s_tank->fish[i].name);
        if (n) ESP_LOGI(TAG, "ours next door: %s", line);
        n = 0;
        for (int k = 0; k < N_VISIT_MAX && n < 280; k++) if (s_tank->vis_used[k])
            n += snprintf(line + n, sizeof line - n, "%s%s (from %08lx, %.0f s here, close %.0f%%%s%s door %d x %.0f)", n ? ", " : "", s_tank->fish[N_FISH_MAX + k].name,
                          (unsigned long)s_tank->vis_home[k], s_tank->clock - s_tank->vis_since[k], 100 * progression_bond(s_tank, s_tank->fish[N_FISH_MAX + k].uid, NULL),
                          s_tank->fish[N_FISH_MAX + k].homeward ? ", HOMEWARD" : "", s_tank->fish[N_FISH_MAX + k].follow_uid ? ", FOLLOWING" : "",
                          s_tank->fish[N_FISH_MAX + k].door, s_tank->fish[N_FISH_MAX + k].x);
        if (n) ESP_LOGI(TAG, "visiting here: %s", line);
        for (int k = 0; k < N_VISIT_MAX; k++) if (s_tank->vis_used[k]) {   /* its passport's HOME line (2026-11-01) */
            char w[PASSPORT_WATERS_MAX]; passport_home_waters(s_tank->vis_home[k], w, sizeof w);
            ESP_LOGI(TAG, "  %s's home: %s%s%s", s_tank->fish[N_FISH_MAX + k].name, s_tank->vis_home_name[k][0] ? s_tank->vis_home_name[k] : w,
                     s_tank->vis_home_name[k][0] ? " - " : "", s_tank->vis_home_name[k][0] ? w : " (its waters: no keeper's name)");
        }
    }
    uint32_t tx, txf, rx, stale, full; link_port_stats(&tx, &txf, &rx, &stale, &full);
    int away = 0; for (int i = 0; i < GHOSTS; i++) away += s_mine[i].st == G_AWAY;
    ESP_LOGI(TAG, "link %s | %s | doors: left %08lx right %08lx%s | ghosts away %d/%d, visitors %d | radio tx %lu (fail %lu) rx %lu stale %lu full %lu | heap int %u KB",
             s_on ? "ON" : "off", BADGE[link_badge(L) % LINK_BADGES],
             (unsigned long)link_neighbor(L, LINK_LEFT), (unsigned long)link_neighbor(L, LINK_RIGHT), link_drifting(L) ? " (paused: drifting)" : "",
             away, GHOSTS, guests(), (unsigned long)tx, (unsigned long)txf, (unsigned long)rx, (unsigned long)stale, (unsigned long)full,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
}

bool link_dev_on(void) { return s_on; }

/* the tank goes to sleep or powers off: a clean goodbye - visitors poof home,
 * our ghosts are recalled, every door closes - then the radio goes DOWN. A
 * radio left up through the sleep grace's light sleep came back deaf and mute
 * while its counters kept counting (2026-10-05, the watch): the link is never
 * left on across a sleep. The keeper links again after the wake. */
static void goodbye(bool tell) {
    if (!s_on) return;
    if (tell) {
        /* RECALL our ghosts and give their RETURNs up to GOODBYE_WAIT_MS to
           arrive (2026-10-05: the radio went down 0.1 s after the RECALL and
           the answer was lost). Anything still out comes home from bubbles on
           the engine's own clock - link_dev_poll keeps ticking it radio-less. */
        link_sleep(L); events();
        for (uint32_t t0 = now_ms(); (int32_t)(now_ms() - t0) < GOODBYE_WAIT_MS; vTaskDelay(pdMS_TO_TICKS(20))) {
            link_port_drain(L, now_ms()); link_tick(L, now_ms()); events();
            if (!ghosts_away() && !fish_away()) break;   /* (2026-11-02: real fish too - they came home 10 s after the wake,
                                                            with the state they left with, when the radio went down first) */
        }
    }
    if (!tell && s_tank)                       /* `link die`: visitors just vanish (their homes' leases run out) */
        for (int k = 0; k < N_VISIT_MAX; k++) if (s_tank->vis_used[k]) { tank_poof(s_tank, s_tank->fish[N_FISH_MAX + k].x, s_tank->fish[N_FISH_MAX + k].y); s_tank->vis_used[k] = false; }
    if (s_tank) tank_link_doors_shut(s_tank);
    s_on = false; radio_stop();
}
/* ---- link mode's page (settings -> LINK) ---- */
bool link_dev_page_up(void) { return s_page; }
static bool any_door(void) { return link_neighbor(L, LINK_LEFT) || link_neighbor(L, LINK_RIGHT); }
static void radio_down_if_idle(void) {          /* nothing open, nothing out, nobody visiting, nobody holding it up: the radio goes off */
    if (s_on && !s_page && !s_dir && !any_door() && !ghosts_away() && !guests() && !fish_away()) {
        s_on = false; radio_stop(); ESP_LOGI(TAG, "link idle: the radio goes down");
    }
}
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
/* THE HANDOVER (the one install, 0.4.3; Strato 11-24 "automatic"): the staged relay goes into the C6 by itself, once,
 * when the tank has run a minute and nothing is on the glass (not in the boot's first seconds: the install page's
 * restarts and a keeper's first look come first). Only if it starts writing does a card take the glass: UPDATING
 * THE LINK, no button; then the tank saves and both chips restart. A failed write: THE LINK UPDATE STOPPED, and only a
 * LINK tap tries again. A LINK tap with no update made yet (or a failed one) asks the handover first: STARTING THE
 * LINK meanwhile, then the link as always if nothing needed writing. (radio_hosted.c does the work, off the tank task) */
static int64_t s_ho_written_us;                /* WRITTEN seen (the restart waits for a flow on the glass, a little) */
static int64_t s_esc_at;                       /* (the escape chain) the restart, once its card has been up long enough to read (0: none) */
void tank_ota_valid_now(const char *why);       /* (main.c) before every restart the tank asks for itself (review 9's L3) */
/* what c6_ho_step answered, done on the tank task: a card, LINK as always, or the restart */
static void ho_act(tank_t *t, int a) {
    switch (a) {
    case C6A_CHECK_WAIT: case C6A_CARD_WAIT: s_page = true; s_ln.card = LN_WAIT; s_ln.down = -1; s_ln_esc = false; render_link_leave();
        ESP_LOGI(TAG, "LINK: check first"); break;
    case C6A_CHECK_ESC: case C6A_CARD_ESC: case C6A_ESCAPE: {   /* (the escape chain) its card: STARTING LINK / YOUR DEVICE MAY RESTART SEVERAL TIMES */
        const bool up = s_page && s_ln.card == LN_WAIT && s_ln_esc;   /* (its card already up: a boot of the chain) */
        s_page = true; s_ln.card = LN_WAIT; s_ln.down = -1; s_ln_esc = true; render_link_leave();
        if (a != C6A_ESCAPE) break;
        /* the chain's restart: its card first, the tank saved - a save that failed restarts nothing (GET HELP: review 9's L2) -
           then 8 s to read it the first time (L4), 3 s on a boot of the chain, then both chips */
        if (!progression_save(t)) { ESP_LOGW(TAG, "LINK: not saved"); ho_act(t, radio_hosted_ho_esc_abort()); break; }   /* (review 10's M1:
                                                  the function leaves ESCAPE - GET HELP, nothing busy) */
        ESP_LOGI(TAG, "LINK: chain restart");
        s_esc_at = esp_timer_get_time() + (up ? 3000000 : 8000000); break; }
    case C6A_CARD_UPDATING: s_page = true; ln_updating(&s_ln); render_link_leave(); s_ho_pct = 0;
        ESP_LOGI(TAG, "LINK: UPDATING YOUR DEVICE"); break;
    case C6A_CARD_NOSTART: s_page = true; s_ln_esc = false; ln_died(&s_ln, true); ESP_LOGW(TAG, "LINK: no answer - THE LINK DID NOT START"); break;
    case C6A_CARD_RETRY: case C6A_CARD_OUT: ln_update_failed(&s_ln, a == C6A_CARD_RETRY); s_page = true;
        ESP_LOGW(TAG, "LINK: THE LINK UPDATE STOPPED (%s)", a == C6A_CARD_RETRY ? "retry" : "get help"); break;
    case C6A_LINK: s_page = false; link_page_begin(t); break;
    case C6A_RESTART: progression_save(t); ESP_LOGI(TAG, "LINK: radio updated - restarting");
        vTaskDelay(pdMS_TO_TICKS(600)); tank_ota_valid_now("the radio's restart"); radio_hosted_restart_clean(); break;   /* (M4: the C6 held, the whole P4 reset - #240) */
    }
}
static void ho_poll(tank_t *t) {                /* the facts, every poll: c6_ho_step decides */
    /* TODO(0.4.4, the audit's M1, deferred by Strato 11-24): on the Tab5, start the automatic check only on the cable with
       the battery above a floor (battery_port), and not record a try otherwise; a LINK tap on battery: a card asking for
       the cable. Until then a power loss mid-write is an interrupted try (c6_ho.h): the next boot tries again. */
    if (s_esc_at && esp_timer_get_time() >= s_esc_at) { s_esc_at = 0; tank_ota_valid_now("the radio's restart"); radio_hosted_restart_clean(); }   /* (saved at C6A_ESCAPE; the card read) */
    if (radio_hosted_handover_state(&s_ho_pct) == HO_WRITTEN && !s_ho_written_us) s_ho_written_us = esp_timer_get_time();
    const bool flow = setup_active() && (!s_ho_written_us || esp_timer_get_time() - s_ho_written_us < 30000000);   /* (M5: at most 30 s) */
    const bool bare = (!touch_port_page_open() || (s_page && s_ln.card == LN_WAIT)) && !setup_active() && !touch_port_confirm_up();
    ho_act(t, radio_hosted_ho_tick(bare, !s_on && !s_page && !touch_port_page_open() && !setup_active(), flow, s_page && s_ln.card == LN_WAIT));
}
#endif
bool link_dev_updating(void) {                  /* (M5) UPDATING YOUR DEVICE is up or the write is under way: no MY TANKS, no birth flow (main.c) */
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
    const int st = radio_hosted_handover_state(NULL);
    return st == HO_WRITING || st == HO_WRITTEN || (s_page && s_ln.card == LN_UPD);
#else
    return false;
#endif
}
bool link_dev_busy(void) {                      /* the radio chip is being written: no sleep, no power-off now (main.c) */
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
    const int st = radio_hosted_handover_state(NULL);
    return st == HO_CHECKING || st == HO_WRITING || st == HO_WRITTEN || st == HO_ESCAPE;
#else
    return false;
#endif
}
void link_dev_page_open(tank_t *t) {
    if (!ensure(t)) return;
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
    if (!s_on) { const int a = radio_hosted_ho_link(); if (a != C6A_LINK) { ho_act(t, a); return; } }   /* (c6_ho.h: the update first?) */
#endif
    link_page_begin(t);
}
static void link_page_begin(tank_t *t) {
#ifdef TANK_P4                                  /* the big screens: the start on the radio task, a card meanwhile (link_note.h) */
    if (!s_on && !(ln_open(&s_ln, radio_begin()) & LN_MODE)) {
        s_page = true; s_ln_esc = false; render_link_leave();
        if (s_ln.card == LN_WAIT) ESP_LOGI(TAG, "LINK: STARTING THE LINK");
        else ESP_LOGW(TAG, "LINK: THE LINK DID NOT START");
        return;
    }
    ln_close(&s_ln);
#else
    if (!s_on && !radio_start()) return;
#endif
    s_on = true; s_page = true; s_last_log = 0; s_page_quiet = now_ms();
    link_mode(L, true); render_link_leave();
    ESP_LOGI(TAG, "link mode up: tank %08lx, badge %s", (unsigned long)t->uid, BADGE[link_badge(L) % LINK_BADGES]);
}
void link_dev_page_close(void) {
    if (!s_page) return;
#ifdef TANK_P4
    if (s_ln.card == LN_UPD) return;          /* (UPDATING YOUR DEVICE stays until the write ends) */
    if (s_ln.card != LN_NONE) {                /* a card, not link mode: nothing to take down (a start under way finishes, then goes down: begun) */
        ln_close(&s_ln); s_page = false; render_link_leave(); ESP_LOGI(TAG, "LINK: the card closed"); return; }
#endif
    s_page = false; render_link_leave();
    if (L) link_mode(L, false);
    ESP_LOGI(TAG, "link mode closed%s", L && any_door() ? " - the doors stay open" : "");
    radio_down_if_idle();
}
/* the page not fed this poll (touch_port: a page or card over it): the note's press is forgotten, as render_link_leave
   forgets link mode's - its lift must not close the card later (the review's B). Off link mode's page: the card goes too (link_note.h). */
void link_dev_touch_cancel(void) {
#ifdef TANK_P4
    ln_cancel(&s_ln, s_page);
#endif
}
#ifdef TANK_P4
/* the start's answer (link_dev_poll, the tank task): link mode's page up, `link on` done, or the radio down again */
static void begun(int r) {
    const int a = ln_result(&s_ln, s_page, r);
    if (r == LP_BEGIN_FAIL) { device_dirty_mask_for_radio(false);
#ifdef CONFIG_POCKET_TANK_RADIO_HOSTED
        if (s_ln.card == LN_FAIL && s_page && radio_hosted_ho_link_failed() == C6A_CARD_OUT) ln_update_failed(&s_ln, false);   /* (H1: the update could not reach this C6) */
#endif
        ESP_LOGW(TAG, "LINK: the radio did not start%s", ln_card(&s_ln, s_page) ? (s_ln.card == LN_UPD_OUT ? " - THE LINK UPDATE STOPPED, no tries left (OK closes it)" : " - THE LINK DID NOT START (OK closes it)") : ""); return; }
    if (a & LN_STOP) { radio_stop(); ESP_LOGI(TAG, "LINK: up, nobody waiting"); return; }
    s_on = true; s_last_log = 0; link_mode(L, true);
    if (a & LN_MODE) { s_page_quiet = now_ms(); render_link_leave(); ESP_LOGI(TAG, "link mode up: tank %08lx, badge %s", (unsigned long)s_me, BADGE[link_badge(L) % LINK_BADGES]); }
    if (a & LN_DIR) { s_dir = true; ESP_LOGI(TAG, "link ON: tank %08lx, badge %s", (unsigned long)s_me, BADGE[link_badge(L) % LINK_BADGES]); }
}
#endif
int link_dev_touch(float x, float y, bool down, float dt) {
#ifdef TANK_P4
    if (ln_card(&s_ln, s_page)) {              /* a card (STARTING / DID NOT START): OK or a tap outside closes it, on the press's lift */
        if (ln_touch(&s_ln, down, render_link_nostart_tap(x, y))) { link_dev_page_close(); return LINK_UI_DONE; }   /* (one card, one OK) */
        return LINK_UI_NONE;
    }
#endif
    if (!s_page || !L) return LINK_UI_NONE;
    link_view_t v; link_view(L, &v);
    if (down) s_page_quiet = now_ms();
    int r = render_link_touch(&v, x, y, down, dt);
    switch (r) {
    case LINK_UI_ARM_LEFT: case LINK_UI_ARM_RIGHT: {
        link_edge_t e = r == LINK_UI_ARM_LEFT ? LINK_LEFT : LINK_RIGHT;
        link_mode(L, true); link_arm(L, e); ESP_LOGI(TAG, "%s arrow held: asking", EDGE[e]); break; }
    case LINK_UI_CLOSE_LEFT: case LINK_UI_CLOSE_RIGHT: {
        link_edge_t e = r == LINK_UI_CLOSE_LEFT ? LINK_LEFT : LINK_RIGHT;
        ESP_LOGI(TAG, "%s door held: closing", EDGE[e]); link_close(L, e); events(); break; }
    case LINK_UI_DONE: link_dev_page_close(); break;
    case LINK_UI_DISCONNECT: link_dev_disconnect(); break;
    case LINK_UI_SD_LEFT: case LINK_UI_SD_RIGHT: {    /* a door's coin (0.4.2): the dial - or why not */
        int e = r == LINK_UI_SD_LEFT ? LINK_LEFT : LINK_RIGHT;
        if (!s_tank) break;
        if (tank_link_sd_pending(s_tank)) { render_link_sd_news(TL_SD_BUSY, e, 0, 0, s_tank->clock); ESP_LOGI(TAG, "sand dollars: a gift still in flight"); break; }
        setup_begin_sdgift(s_tank, e, v.edge[e].badge); s_sd_peer = link_neighbor(L, (link_edge_t)e); s_sd_dial = true;
        ESP_LOGI(TAG, "sd: dial for %s (balance %ld)", EDGE[e], (long)s_tank->sd_balance);
        break; }
    }
    return r;
}
/* the dial's SEND (0.4.2): the money leaves, saved with its note, and goes */
void link_dev_sd_send(tank_t *t, int edge, int amount) {
    if (!t) return;
    int k = L && s_on && !(s_sd_dial && !s_sd_peer) ? tank_link_sd_send(t, L, edge, s_sd_peer, amount) : TL_SD_NO_DOOR;   /* (to the tank the dial was
                                                                       opened for; a dial opened at no tank sends nothing - the review's 4) */
    s_sd_dial = false;
    ESP_LOGI(TAG, "sd: SEND %d via %s -> %d (balance %ld)", amount, edge == LINK_LEFT ? "left" : "right", k, (long)t->sd_balance);
    render_link_sd_news(k, edge, amount, 0, t->clock);
    s_page_quiet = now_ms();
}
/* DISCONNECT (the DRIFTED notice's button, link mode's chip): every door of
   this tank shuts now - visitors poof home, ours are called back */
void link_dev_disconnect(void) {
    if (!L || !s_on) return;
    ESP_LOGI(TAG, "DISCONNECT: every door closes");
    link_close(L, LINK_LEFT); link_close(L, LINK_RIGHT); events();
    link_tick(L, now_ms()); events();          /* (as the sleep's goodbye: the gift timers see the doors shut - an ask let go, a TAKEN
                                                  gift kept - before the radio goes; 0.4 rc round two) */
    radio_down_if_idle();
}
bool link_dev_drift_hit(float x, float y) {
    if (!L || !s_on) return false;
    link_view_t v; link_view(L, &v);
    return render_link_drift_hit(&v, x, y);
}
void link_dev_draw(uint16_t *fb, int stride, double clock) {
#ifdef TANK_P4
    if (ln_card(&s_ln, s_page)) {
        if (s_ln.card == LN_WAIT) { if (s_ln_esc) render_link_escape(fb, stride); else render_link_wait(fb, stride); }
        else if (s_ln.card == LN_UPD) render_link_updating(fb, stride, s_ho_pct);
        else if (s_ln.card == LN_UPD_FAIL || s_ln.card == LN_UPD_OUT) render_link_update_failed(fb, stride, s_ln.card == LN_UPD_FAIL);
        else render_link_nostart(fb, stride);
        return;
    }
#endif
    if (!L || !s_on) return;
    link_view_t v; link_view(L, &v);
    if (s_page) render_link_mode(&v, fb, stride, clock);
    else render_link_doors(&v, fb, stride, clock);
    render_link_drift(&v, fb, stride, clock);
}

void link_dev_sleep(void) { s_page = false;
#ifdef TANK_P4
    ln_sleep(&s_ln);
#endif
    render_link_leave(); s_dir = false; if (s_on) { ESP_LOGI(TAG, "sleep: the link closes, the radio goes down"); goodbye(true); }
#ifdef TANK_P4
    link_port_settle(2000);                    /* the goodbye goes to the C6 on the radio task: it gets there before the tank sleeps or powers off */
#endif
}

/* link book: the tank's own visitor collection and gift notes (0.4: they move with it on MY TANKS) - the link up or not */
static void link_book(tank_t *t) {
    char names[96] = ""; int n = collection_count();
    for (int k = 0; k < n; k++) { const coll_entry_t *e = collection_get(k); if (strlen(names) + strlen(e->name) + 2 < sizeof names) { if (k) strcat(names, " "); strcat(names, e->name); } }
    gift_book_t *g = gift_of(t);
    char nm[sizeof shelf_now()->name]; memcpy(nm, shelf_now()->name, sizeof nm); nm[sizeof nm - 1] = 0;
    for (char *q = nm; *q; q++) if (*q == '\n') *q = '/';   /* (a two-line name, on one log line) */
    ESP_LOGI(TAG, "book: %s %d collected (%s), notes given/arriving/received %d/%d/%d", nm, n, n ? names : "none",
             g ? g->n_given : 0, g ? g->n_arr : 0, g ? g->n_recv : 0);
    char kn[PASSPORT_TANK_NAME], w[PASSPORT_WATERS_MAX]; shelf_keeper_name(kn); passport_home_waters(t->uid, w, sizeof w);
    ESP_LOGI(TAG, "book: %08lx as '%s', waters %s", (unsigned long)t->uid, kn, w);
}
void link_dev_cmd(tank_t *t, int argc, char **argv) {
    const char *c = argc > 1 ? argv[1] : "";
    int e = argc > 2 && !strcmp(argv[2], "left") ? LINK_LEFT : argc > 2 && !strcmp(argv[2], "right") ? LINK_RIGHT : -1;
    if (!strcmp(c, "page")) {                 /* link mode's page, as the settings LINK button opens it (DONE / `link page` closes) */
        if (link_dev_page_up()) link_dev_page_close(); else link_dev_page_open(t);
        return;
    }
    if (!strcmp(c, "sd")) {                   /* link sd [left|right [n | dial]]: sand dollar gifts (0.4.2) - status, send n, or open the dial */
        if (e >= 0 && argc > 3 && !strcmp(argv[3], "dial")) { if (link_dev_page_up() && !setup_active()) { setup_begin_sdgift(t, e, L ? link_peer_badge(L, link_neighbor(L, (link_edge_t)e)) : 0); s_sd_peer = L ? link_neighbor(L, (link_edge_t)e) : 0; s_sd_dial = true; } else ESP_LOGW(TAG, "link sd: `link page` first"); return; }
        if (e >= 0 && argc > 3) { s_sd_peer = 0; s_sd_dial = false; link_dev_sd_send(t, e, atoi(argv[3])); return; }
        const sdg_book_t *b = &t->sdg;
        ESP_LOGI(TAG, "sd: balance %ld, given %lu, in flight %s", (long)t->sd_balance, (unsigned long)b->seq, b->out.nonce ? "yes" : "none");
        if (b->out.nonce) ESP_LOGI(TAG, "  out: %u to %08lx (gift %lu), engine %s, door %s", b->out.amount, (unsigned long)b->out.peer, (unsigned long)b->out.nonce,
                                   L && link_sd_busy(L) ? "sending" : "idle", L && (link_neighbor(L, LINK_LEFT) == b->out.peer || link_neighbor(L, LINK_RIGHT) == b->out.peer) ? "open" : "closed");
        for (int i = 0; i < SDG_SEEN_N; i++) if (b->seen[i].peer)
            ESP_LOGI(TAG, "  seen: %08lx gift %lu: %u, verdict %u, age %u", (unsigned long)b->seen[i].peer, (unsigned long)b->seen[i].nonce, b->seen[i].amount, b->seen[i].verdict, b->seen[i].age);
        for (int k = 0; k < LINK_EDGES; k++) if (L && link_neighbor(L, (link_edge_t)k))
            ESP_LOGI(TAG, "  %s door: %08lx takes gifts: %s", EDGE[k], (unsigned long)link_neighbor(L, (link_edge_t)k), link_peer_sd(L, link_neighbor(L, (link_edge_t)k)) ? "yes" : "no");
        return;
    }
    if (!strcmp(c, "chan")) {                 /* link chan [n]: the ESP-NOW channel (the test of 2026-10-12: a row on a channel that is not 1) */
        if (argc > 2) { if (!lp_radio_set_channel(atoi(argv[2]))) ESP_LOGW(TAG, "link chan <1-13>"); }
        else ESP_LOGI(TAG, "channel %d", lp_radio_channel());
        return;
    }
    if (!strcmp(c, "on")) {
        if (!ensure(t)) return;
#ifdef TANK_P4
        if (!s_on) {
            if (s_ln.pending) { s_ln.want_dir = true; ESP_LOGI(TAG, "link: radio starting"); return; }
            const int r = radio_begin();
            if (!(ln_dir(&s_ln, r) & LN_DIR)) { if (r == LP_BEGIN_WAIT) ESP_LOGI(TAG, "link: radio starting."); return; }
        }
#else
        if (!s_on && !radio_start()) return;
#endif
        s_on = true; s_dir = true; link_mode(L, true); s_last_log = 0;
        ESP_LOGI(TAG, "link ON: tank %08lx, badge %s", (unsigned long)t->uid, BADGE[link_badge(L) % LINK_BADGES]);
    } else if (!strcmp(c, "off")) {
#ifdef TANK_P4
        ln_off(&s_ln);                         /* (a `link on` still waiting on the start: called off) */
#endif
        if (!s_on) { ESP_LOGI(TAG, "link is off"); return; }
        s_dir = false; s_page = false; render_link_leave(); link_dev_touch_cancel();
        goodbye(true);
        ESP_LOGI(TAG, "link OFF%s", ghosts_away() ? " - a ghost still out comes home from bubbles within 10 s" : "");
    } else if (!strcmp(c, "die")) {           /* a sudden death for the tests: the radio just stops - no goodbye */
#ifdef TANK_P4
        ln_off(&s_ln);
#endif
        if (!s_on) { ESP_LOGI(TAG, "link is off"); return; }
        s_dir = false; s_page = false; render_link_leave(); link_dev_touch_cancel();
        goodbye(false);
        ESP_LOGI(TAG, "link DIED");
    } else if (!s_on) {
        if (!strcmp(c, "status")) link_dev_status();
        else if (!strcmp(c, "book")) link_book(t);
        else ESP_LOGW(TAG, "link is off: link on first");
    } else if (!strcmp(c, "arm") && e >= 0 && link_neighbor(L, (link_edge_t)e)) {
        ESP_LOGI(TAG, "%s door already open (%08lx)", EDGE[e], (unsigned long)link_neighbor(L, (link_edge_t)e));
    } else if (!strcmp(c, "arm") && e >= 0) { link_mode(L, true); link_arm(L, (link_edge_t)e);   /* (link mode closes itself after 2 idle minutes: an arrow opens it again) */ ESP_LOGI(TAG, "%s arrow held (asking for %d s)", EDGE[e], LINK_ARM_MS / 1000); }
    else if (!strcmp(c, "close") && e >= 0) { link_close(L, (link_edge_t)e); events(); }
    else if (!strcmp(c, "ghost") && e >= 0) {
        int g = -1; for (int i = 0; i < GHOSTS && g < 0; i++) if (s_mine[i].st == G_HOME) g = i;
        if (g < 0) { ESP_LOGW(TAG, "both ghosts are out"); return; }
        if (!link_door_open(L, (link_edge_t)e)) { ESP_LOGW(TAG, "the %s door is not open", EDGE[e]); return; }
        s_mine[g].st = G_OFFERED;
        link_offer(L, (link_edge_t)e, &s_mine[g].pp, 0.4f, e == LINK_RIGHT ? 0.0f : 3.14159f, 40);
        ESP_LOGI(TAG, "ghost %s heads for the %s door", s_mine[g].pp.name, EDGE[e]);
    } else if (!strcmp(c, "home")) {           /* every visitor here heads home (as the drift rule sends them) */
        int n = 0; if (s_tank) for (int k = 0; k < N_VISIT_MAX; k++) if (s_tank->vis_used[k]) { s_tank->fish[N_FISH_MAX + k].homeward = true; n++; }
        ESP_LOGI(TAG, "%d visitor%s heading home", n, n == 1 ? "" : "s");
    } else if (!strcmp(c, "visit") && argc > 3) {   /* link visit <fish 0-5> left|right: one of OUR real fish heads for that door */
        int i = atoi(argv[2]); int ed = !strcmp(argv[3], "left") ? LINK_LEFT : LINK_RIGHT;
        if (!s_tank || i < 0 || i >= s_tank->n_fish || s_tank->fish[i].away) { ESP_LOGW(TAG, "no such fish here"); return; }
        if (!link_door_open(L, (link_edge_t)ed)) { ESP_LOGW(TAG, "the %s door is not open", EDGE[ed]); return; }
        fish_t *f = &s_tank->fish[i]; f->goal.id = GOAL_EXPLORE; f->door = DOOR_HEADING; f->door_edge = (int8_t)ed; f->door_refused = 0;
        ESP_LOGI(TAG, "%s heads for the %s door", f->name, EDGE[ed]);
    }
    else if (!strcmp(c, "give") && argc > 2) {   /* link give <fish 0-5 | name> [left|right]: GIVE confirmed in its modal (no side:
                                                    the open door nearest the fish - tank_link's gift_door) */
        gift_book_t *g = gift_of(t);
        int i = atoi(argv[2]);
        if (argv[2][0] < '0' || argv[2][0] > '9') { i = -1; for (int j = 0; j < t->n_fish && i < 0; j++) if (!strcmp(t->fish[j].name, argv[2])) i = j; }
        if (!g || i < 0 || i >= t->n_fish) { ESP_LOGW(TAG, "give: no such fish here (or no gift notes)"); return; }
        if (!progression_fish_givable(t, i)) { ESP_LOGW(TAG, "give: %s cannot go now", t->fish[i].name); return; }
        int side = argc > 3 ? (!strcmp(argv[3], "left") ? GIFT_DOOR_LEFT : !strcmp(argv[3], "right") ? GIFT_DOOR_RIGHT : -1) : GIFT_DOOR_ANY;
        if (side < 0) { ESP_LOGW(TAG, "give: the side is left or right"); return; }
        g->want_uid = t->fish[i].uid; g->want_door = (uint8_t)side; g->want_peer = 0;
        ESP_LOGI(TAG, "give: %s offered next door (%s)", t->fish[i].name, side == GIFT_DOOR_LEFT ? "left" : side == GIFT_DOOR_RIGHT ? "right" : "the nearest open door");
    }
    else if (!strcmp(c, "bake") && argc > 2) {   /* link bake on|off: the GIVE modal and the gift card from their bakes, or drawn whole */
        render_modal_cache_on(!strcmp(argv[2], "on")); ESP_LOGI(TAG, "bake: the cards %s", !strcmp(argv[2], "on") ? "from their bakes" : "drawn whole");
    }
    else if (!strcmp(c, "modal") && argc > 2) {  /* link modal <fish 0-5> | off: open the GIVE modal as its GIVE box would (staging, frame rate) */
        if (!strcmp(argv[2], "off")) { render_give_modal_close(); ESP_LOGI(TAG, "modal: closed"); return; }
        int i = atoi(argv[2]);
        if (i < 0 || i >= t->n_fish) { ESP_LOGW(TAG, "modal: no such fish"); return; }
        render_give_tap(t, i);
        ESP_LOGI(TAG, "modal: %s's GIVE modal %s (step %d)", t->fish[i].name, render_give_modal_step(t) ? "up" : "not up (unlinked?)", render_give_modal_step(t));
    }
    else if (!strcmp(c, "accept") || !strcmp(c, "refuse")) {   /* link accept | refuse: our keeper's answer to a gift */
        gift_book_t *g = gift_of(t);
        if (!g || !g->ask.on) { ESP_LOGW(TAG, "%s: no gift is being offered", c); return; }
        g->ask.answer = !strcmp(c, "accept") ? 1 : -1;
    }
    else if (!strcmp(c, "gifts")) {               /* link gifts: the notes and what is in flight */
        gift_book_t *g = gift_of(t);
        if (!g) { ESP_LOGI(TAG, "gifts: no book (this tank cannot give or take)"); return; }
        ESP_LOGI(TAG, "gifts given/arriving/received %d/%d/%d | asking %08lx leaving %08lx | q %s", g->n_given, g->n_arr, g->n_recv,
                 (unsigned long)g->asking_uid, (unsigned long)g->leaving_uid, g->ask.on ? g->ask.pp.name : "none");
        for (int k = 0; k < g->n_given; k++) ESP_LOGI(TAG, "  GIVEN %s %08lx to %08lx", g->given[k].pp.name, (unsigned long)g->given[k].uid, (unsigned long)g->given[k].peer);
        for (int k = 0; k < g->n_arr; k++) ESP_LOGI(TAG, "  ARRIVING %s %08lx from %08lx", g->arriving[k].pp.name, (unsigned long)g->arriving[k].uid, (unsigned long)g->arriving[k].peer);
    }
    else if (!strcmp(c, "book")) link_book(t);
    else if (!strcmp(c, "drift") && argc > 2) { s_drift = !strcmp(argv[2], "on") ? 1 : !strcmp(argv[2], "off") ? 0 : -1; ESP_LOGI(TAG, "drift: %s", s_drift < 0 ? "the IMU" : s_drift ? "staged ON" : "staged off"); }
    else if (!strcmp(c, "status")) link_dev_status();
    else if (!strcmp(c, "fry")) {              /* a cross-tank fry now (phase 5): the first visitor courts our best */
        int k = -1; for (int j = 0; j < N_VISIT_MAX && k < 0; j++) if (t->vis_used[j] && !t->fish[N_FISH_MAX + j].homeward) k = j;
        if (k < 0) { ESP_LOGI(TAG, "fry: no visitor here"); return; }
        bool ours = false; for (int i = 0; i < t->n_fish; i++) ours |= !t->fish[i].away;
        if (!ours) { ESP_LOGI(TAG, "fry: none of ours is here to court"); return; }
        t->fry_partner = t->fish[N_FISH_MAX + k].uid; t->fry_asked = false; t->fry_home_room = -1;
        t->fry_force = argc > 2 ? (!strcmp(argv[2], "home") ? 1 : 0) : -1;
        s_fry_at = now_ms() + 2000;
        ESP_LOGI(TAG, "fry: %s asks its home for room (%s)", t->fish[N_FISH_MAX + k].name, t->fry_force < 0 ? "free" : t->fry_force ? "home" : "ours");
    }
    else ESP_LOGI(TAG, "link page | link on|off|die | arm left|right | close left|right | ghost left|right | visit <fish> left|right | give <fish> [left|right] | modal <fish>|off | accept | refuse | gifts | home | drift on|off|imu | status | spike ...");
}
