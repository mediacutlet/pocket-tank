/* tank_link.c — the tank's side of the tank link (tank_link.h). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "tank_link.h"
#include "progression.h"
#include "collection.h"
#include "gift.h"
#include "shelf.h"

static float clampf_(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float glass_x(int e) { return e == LINK_LEFT ? tank_glass_x0(DOOR_Y) : tank_glass_x1(DOOR_Y); }
static int home_slot(const tank_t *t, uint32_t uid) {
    for (int i = 0; i < t->n_fish; i++) if (t->fish[i].uid == uid) return i;
    return -1;
}
static int vis_slot(const tank_t *t, uint32_t uid) {
    for (int k = 0; k < N_VISIT_MAX; k++) if (t->vis_used[k] && t->fish[N_FISH_MAX + k].uid == uid) return N_FISH_MAX + k;
    return -1;
}
static float depth_of(const fish_t *f) { return clampf_(f->y / (float)TANK_BOT, 0, 1); }
static float y_of(float depth) {                         /* a depth on another glass, inside our door band */
    return clampf_(depth * TANK_BOT, DOOR_Y - DOOR_BAND_H / 2 + 18, DOOR_Y + DOOR_BAND_H / 2 - 18);
}
static bool beyond(const fish_t *f) {                    /* through the glass by a body: gone from this side */
    float m = 14 * f->size;
    return f->door_edge == LINK_LEFT ? f->x < glass_x(LINK_LEFT) - m : f->x > glass_x(LINK_RIGHT) + m;
}
/* a fish arriving at door e: just outside the glass, at its depth, swimming in */
static void arrive_at(fish_t *f, int e, float depth, float speed) {
    f->x = glass_x(e) + (e == LINK_LEFT ? -16 * f->size : 16 * f->size);
    f->y = y_of(depth);
    f->heading = e == LINK_LEFT ? 0 : 3.14159265f;
    tank_fish_face(f);
    f->speed = f->target_speed = clampf_(speed, 20, 50);
    f->door = DOOR_ENTERING; f->door_edge = (int8_t)e; f->door_sent = false;
    f->explore_set = false;
}

int tank_link_room(void *ctx) {
    const tank_t *t = ctx;
    int swim = 0, free_k = 0;
    for (int i = 0; i < N_FISH_SLOTS; i++) swim += tank_swims(t, i);
    for (int k = 0; k < N_VISIT_MAX; k++) free_k += !t->vis_used[k] && !t->vis_pending[k];
    int room = TANK_LINK_SWIM_MAX - swim;
    return room < free_k ? (room < 0 ? 0 : room) : free_k;
}
/* a passport for a fish here, with its home tank's name: ours as the keeper named it, a visitor's as it came */
static void pp_of(const tank_t *t, passport_t *pp, const fish_t *f, uint32_t home, float age_s) {
    passport_from_fish(pp, f, home, age_s);
    if (home == t->uid) { shelf_keeper_name(pp->home_name); return; }
    for (int k = 0; k < N_VISIT_MAX; k++)
        if (t->vis_home[k] == home && t->vis_home_name[k][0]) { passport_clean_tank_name(pp->home_name, t->vis_home_name[k]); return; }
}
bool tank_link_visitor(void *ctx, uint32_t uid, passport_t *pp) {
    tank_t *t = ctx;
    int s = vis_slot(t, uid);
    if (s < 0) return false;
    int k = s - N_FISH_MAX;
    pp_of(t, pp, &t->fish[s], t->vis_home[k], t->vis_age[k]);
    return true;
}
static int fry_room(void *ctx) { return progression_fry_room((const tank_t *)ctx); }
static int give_state(void *ctx, uint32_t uid, uint32_t nonce, passport_t *pp) { return gift_state(gift_of(ctx), uid, nonce, pp); }
/* home: the place held for a fry of ours courting next door (tank_t.fry_hold; link_host_t.fry_hold). One a neighbor:
   asked again, it is renewed (for whichever of ours courts there now); a new one only with room - fish_held counts it */
static int fry_hold(void *ctx, uint32_t peer, uint32_t parent, bool on) {
    tank_t *t = ctx;
    int h = -1;
    for (int i = 0; i < TANK_FRY_HOLDS; i++) if (t->fry_hold[i].peer == peer) h = i;
    if (!on) { if (h >= 0 && t->fry_hold[h].parent == parent) t->fry_hold[h].peer = t->fry_hold[h].parent = 0; return 0; }
    if (h < 0 && progression_fry_room(t)) for (int i = 0; i < TANK_FRY_HOLDS && h < 0; i++) if (!t->fry_hold[i].peer) h = i;
    if (h < 0) return 0;
    t->fry_hold[h].peer = peer; t->fry_hold[h].parent = parent; t->fry_hold[h].until = t->clock + TANK_LINK_FRY_HOLD_S;
    return 1;
}
link_host_t tank_link_host(tank_t *t, void (*send)(void *ctx, uint32_t to, const uint8_t *buf, int len)) {
    link_host_t h = { t, send, tank_link_room, tank_link_visitor, fry_room, give_state, fry_hold };
    return h;
}

/* one of ours, home again: what it lived next door comes with it (drives,
   meals, badges, its temper's drift, its age); what is this tank's stays
   (its trust in our keeper, its meals from our hand, its spot by the reef) */
static void welcome_home(tank_t *t, int i, const passport_t *pp) {
    fish_t *f = &t->fish[i];
    progression_settings_changed();                  /* (home again: saved soon) */
    if (pp && pp->uid == f->uid) {
        f->hunger = pp->hunger; f->energy = pp->energy; f->stress = pp->stress; f->curiosity = pp->curiosity; f->bored = pp->bored;
        f->sociable = pp->sociable; f->bold = pp->bold; f->drift_acc = pp->drift_acc;
        if (pp->eaten > f->eaten) f->eaten = pp->eaten;
        f->ms_bits |= pp->ms_bits;
        progression_age_from_passport(t, i, pp->age_s);   /* (its stage kept: a visit never demotes - an elder grown
                                                             under 0.3.4's shorter times came home an adult, 11-03 rc) */
    }
    f->away = false; f->homeward = false; f->door_sent = false;
    f->door_seen = t->clock;
    f->sig = 0xffffffffu; f->ask_age = 99;           /* the advisor asks about it soon */
}

void tank_link_doors_shut(tank_t *t) {
    t->door_open[LINK_LEFT] = t->door_open[LINK_RIGHT] = false;
    for (int i = 0; i < N_FISH_SLOTS; i++) {
        fish_t *f = &t->fish[i];
        if (!tank_swims(t, i)) continue;
        if (f->door == DOOR_HEADING || f->door == DOOR_ASKED || (f->door == DOOR_PASSING && i < N_FISH_MAX)) { f->door = DOOR_NONE; f->door_sent = false; }
    }
}

/* ---- GIVE (phase 6; gift.h): the keeper's taps, the question on our glass, a given fish leaving ---- */
static int edge_of(const link_t *L, uint32_t peer) {
    return !L || !peer ? -1 : link_neighbor(L, LINK_LEFT) == peer ? LINK_LEFT : link_neighbor(L, LINK_RIGHT) == peer ? LINK_RIGHT : -1;
}
static void gift_said(tank_t *t, gift_book_t *g, uint32_t uid, int reason) { g->said_uid = uid; g->said_reason = (uint8_t)reason; g->said_until = t->clock + GIFT_SAID_S; }
/* an accepted gift that never left: ours after all - the other tank has nothing alive until GIVEN, and its
   TAKE (now, or at the next link) is answered CANCEL. A fish on its way to the door turns back */
static void gift_keep(tank_t *t, link_t *L, gift_book_t *g, int i) {
    if (L) link_give_keep(L, g->leaving_uid, g->leaving_nonce);
    if (i >= 0) {
        fish_t *f = &t->fish[i];
        if (!f->away && f->door != DOOR_ENTERING) { f->door = DOOR_NONE; f->door_sent = false; f->door_refused = t->clock + TANK_LINK_REFUSED_S; }
        f->deported = false;
    }
    gift_said(t, g, g->leaving_uid, LINK_GIVE_NO_OTHER);
    g->leaving_uid = 0; g->leaving_here = false;
}
/* the gift leaves: its GIVEN note on the flash FIRST, the fish out in the same step, then GIVEN to the tank
   next door. The one moment it is theirs - so it comes only while that tank is linked and heard (gift_tick) */
static void gift_commit(tank_t *t, link_t *L, gift_book_t *g, int i, bool bubbles) {
    const fish_t *f = &t->fish[i];
    uint32_t uid = g->leaving_uid, nonce = g->leaving_nonce;
    passport_t pp = g->leaving_pp;                   /* (next door: the host's latest, as the engine had it) */
    if (!f->away) pp_of(t, &pp, f, t->uid, progression_age_s(t, i));
    if (!progression_keeps_two(t, i)) { gift_keep(t, L, g, i); return; }   /* (the last pair stays - checked again at the door: 11-02 sweep) */
    if (!gift_given_add(g, uid, nonce, g->leaving_peer, &pp)) { gift_keep(t, L, g, i); return; }   /* (no note on the flash: it stays) */
    g->leaving_uid = 0; g->leaving_here = false;
    if (bubbles) tank_poof(t, f->x, f->y);
    progression_give_out(t, i);
    link_give_done(L, uid, nonce);
}
/* the door a gift goes out by - the one place it is chosen (2026-11-02, Strato: a tank in the middle lets the keeper
   pick; it always went LEFT): the keeper's pick if that door is open to the tank picked, else none (-1: the card says
   it cannot go); GIFT_DOOR_ANY (the director's `link give` without a side) the open door nearest the fish */
static int gift_door(const tank_t *t, const link_t *L, const gift_book_t *g, const fish_t *f) {
    if (g->wait_door != GIFT_DOOR_ANY) {
        int e = g->wait_door - 1;
        if (e < 0 || e >= LINK_EDGES || !t->door_open[e]) return -1;
        return !g->wait_peer || link_neighbor(L, (link_edge_t)e) == g->wait_peer ? e : -1;
    }
    if (t->door_open[LINK_LEFT] && t->door_open[LINK_RIGHT]) return f->x < TANK_W * 0.5f ? LINK_LEFT : LINK_RIGHT;
    return t->door_open[LINK_LEFT] ? LINK_LEFT : t->door_open[LINK_RIGHT] ? LINK_RIGHT : -1;
}
static void gift_tick(tank_t *t, link_t *L, gift_book_t *g) {
    if (g->n_arr) {                                  /* ARRIVING notes held past their time, the giver not next door: let go (11-02 sweep) */
        float dt = (float)(t->clock - g->arr_aged_at); if (dt < 0 || dt > 5) dt = 0;
        if (gift_arriving_age(g, dt))
            for (int k = g->n_arr - 1; k >= 0; k--) {
                const gift_note_t *n = &g->arriving[k];
                if (n->held < GIFT_ARRIVING_TTL_S || (L && (link_neighbor(L, LINK_LEFT) == n->peer || link_neighbor(L, LINK_RIGHT) == n->peer))) continue;
                uint32_t uid = n->uid, nonce = n->nonce;
                if (L) link_give_forget(L, uid, nonce);
                gift_arriving_drop(g, uid, nonce);
            }
    }
    g->arr_aged_at = t->clock;
    if (!L) {                                        /* no engine: no question stands, nothing is being asked, nothing leaves */
        g->ask.on = false; g->asking_uid = 0; g->want_uid = g->cancel_uid = g->wait_uid = 0;
        if (g->leaving_uid) gift_keep(t, NULL, g, home_slot(t, g->leaving_uid));
        return;
    }
    if (g->asking_uid && (t->clock - g->asking_since > (LINK_GIVE_ASK_MS + 5000) / 1000.0 || t->clock < g->asking_since))
        g->asking_uid = 0;                           /* (a stale ask: the engine lets go of its own by LINK_GIVE_ASK_MS + 2 s - never one
                                                        lost event blocking every gift after it, 11-03 rc) */
    if (g->want_uid) {                               /* the keeper's second tap on GIVE: offered as soon as the fish is somewhere it can go from */
        g->wait_uid = g->want_uid; g->wait_since = t->clock; g->want_uid = 0;
        g->wait_door = g->want_door; g->wait_peer = g->want_peer; g->want_door = GIFT_DOOR_ANY; g->want_peer = 0;
    }
    if (g->cancel_uid) {
        if (g->cancel_uid == g->asking_uid) link_give_cancel(L, g->cancel_uid);
        if (g->cancel_uid == g->wait_uid) g->wait_uid = 0;
        g->cancel_uid = 0;
    }
    if (g->wait_uid) {
        uint32_t uid = g->wait_uid;
        int i = home_slot(t, uid);
        if (i < 0 || !progression_fish_givable(t, i)) g->wait_uid = 0;   /* (gone, or the keeper's own rules now say no: the card says why) */
        else if (g->leaving_uid || g->asking_uid) { g->wait_uid = 0; gift_said(t, g, uid, LINK_GIVE_NO_OTHER); }   /* (one gift asked or on its
                                                     way out at a time: a second GIVE while one asked took over asking_uid - the first
                                                     sellable, its CALL OFF dead, 11-03 rc) */
        else {
            fish_t *f = &t->fish[i];
            /* home and clear of the doors: through the door gift_door picks; next door: to the tank it is visiting, where it is
               (2026-11-01, Strato: a fish that swam off on its own must not stop the gift); in a doorway, crossing either way: a
               moment more. The tank the keeper picked no longer there: it cannot go (the modal says so) */
            int e = f->away ? link_away_edge(L, uid)
                  : (f->door == DOOR_NONE || f->door == DOOR_HEADING) ? gift_door(t, L, g, f) : -2;
            if (e >= 0 && g->wait_peer && link_neighbor(L, (link_edge_t)e) != g->wait_peer) { g->wait_uid = 0; gift_said(t, g, uid, LINK_GIVE_NO_OTHER); }
            else if (e >= 0) {
                g->wait_uid = 0;
                if (!f->away && f->door == DOOR_HEADING) f->door = DOOR_NONE;   /* (on its way to a door: the gift calls it back) */
                passport_t pp; pp_of(t, &pp, f, t->uid, progression_age_s(t, i));   /* (away: the engine gives the host's latest) */
                if (link_give(L, (link_edge_t)e, &pp)) { g->asking_uid = uid; g->asking_nonce = link_give_nonce(L, uid); g->asking_since = t->clock; g->peer_badge = (int8_t)link_peer_badge(L, link_neighbor(L, (link_edge_t)e)); }
                else { g->said_uid = uid; g->said_reason = LINK_GIVE_NO_OTHER; g->said_until = t->clock + GIFT_SAID_S; }
            } else if (e == -1 && !f->away) { g->wait_uid = 0; g->said_uid = uid; g->said_reason = LINK_GIVE_NO_OTHER; g->said_until = t->clock + GIFT_SAID_S; }   /* (no door open) */
            else if (t->clock - g->wait_since > GIFT_WAIT_S) { g->wait_uid = 0; g->said_uid = uid; g->said_reason = LINK_GIVE_NO_OTHER; g->said_until = t->clock + GIFT_SAID_S; }
        }
    }
    if (g->ask.on && g->ask.answer) {               /* our keeper answered: ACCEPT saves the ARRIVING note first */
        g->ask.on = false;
        if (g->ask.answer > 0 && progression_fry_room(t) && gift_arriving_add(g, g->ask.uid, g->ask.nonce, g->ask.peer, &g->ask.pp)) {
            if (!link_give_answer(L, g->ask.uid, g->ask.nonce, true)) gift_arriving_drop(g, g->ask.uid, g->ask.nonce);   /* (it had closed) */
        } else if (g->ask.answer > 0)                /* a yes we cannot keep: the giver hears why, not NO THANKS (11-02 sweep) */
            link_give_refuse(L, g->ask.uid, g->ask.nonce, progression_fry_room(t) ? LINK_GIVE_NO_OTHER : LINK_GIVE_NO_FULL);
        else link_give_answer(L, g->ask.uid, g->ask.nonce, false);
    }
    if (g->leaving_uid) {                            /* accepted next door: out through the door, then gone */
        int i = home_slot(t, g->leaving_uid);
        /* the tank it goes to must still be THERE - linked, and heard lately - up to the moment it leaves: a reset, a
           door closed, a sleep, a dead battery or a walk out of range before then and it stays ours (2026-11-02: it
           used to leave in bubbles with its note and live nowhere until the two tanks happened to link again) */
        if (i < 0 || edge_of(L, g->leaving_peer) < 0 || !link_peer_heard(L, g->leaving_peer, GIFT_HEARD_MS)) { gift_keep(t, L, g, i); return; }
        fish_t *f = &t->fish[i];
        const bool fresh = link_peer_heard(L, g->leaving_peer, GIFT_COMMIT_MS);   /* (the moment it is theirs: only just heard from) */
        if (f->away) {                               /* given where it is: the stamp a moment, then it is theirs - nothing swims */
            if (t->clock - g->leaving_since > GIFT_HERE_S && fresh) gift_commit(t, L, g, i, false);
            return;
        }
        bool open = g->leaving_edge >= 0 && t->door_open[g->leaving_edge];
        if (open) {                                  /* briskly to its door (the DEPORT swim), then through it - no OFFER: it is theirs */
            if (f->door == DOOR_NONE || f->door == DOOR_HEADING) { f->door = DOOR_HEADING; f->door_edge = g->leaving_edge; f->goal.id = GOAL_EXPLORE; f->deported = true; f->door_refused = 0; }
            else if (f->door == DOOR_ASKED) { f->door = DOOR_PASSING; f->door_sent = true; }
        }
        bool through = open && f->door == DOOR_PASSING && beyond(f);
        if ((through || !open || t->clock - g->leaving_since > GIFT_LEAVE_S + TANK_W / 55.0f) && fresh)   /* (a floor's length at the brisk
                                                                                                  swim; !open: the doors paused by a drift, still linked) */
            gift_commit(t, L, g, i, !through);
    }
}

void tank_link_tick(tank_t *t, link_t *L) {
    gift_book_t *gb = gift_of(t);
    progression_cap_check(t, "tank_link_tick");      /* (the link's paths, once a tick: every tank here, host or home) */
    if (gb) gift_tick(t, L, gb);
    /* a boot that found fish away: home from bubbles once the host's lease is
       surely over (they may come back by the door before that, if linked) */
    if (t->link_restore_at > 0 && t->clock >= t->link_restore_at) {
        t->link_restore_at = 0;
        for (int i = 0; i < t->n_fish; i++) {
            fish_t *f = &t->fish[i];
            if (!f->away || (L && link_is_away(L, f->uid))) continue;
            f->x = TANK_W * 0.5f; f->y = DOOR_Y; tank_glass_clamp(&f->x, &f->y, 30);
            welcome_home(t, i, NULL);
            tank_poof(t, f->x, f->y);
        }
    }
    /* home: a held place ends when its time runs out, the engine is gone, or that fish of ours is no longer next
       door (home again: the fry it was held for cannot come any more). A drift's paused door keeps it */
    for (int h = 0; h < TANK_FRY_HOLDS; h++)
        if (t->fry_hold[h].peer && (!L || t->clock > t->fry_hold[h].until || !link_is_away(L, t->fry_hold[h].parent)))
            t->fry_hold[h].peer = t->fry_hold[h].parent = 0;
    if (!L) {
        /* the radio went down with a fry waiting: not yet offered = ours (if there is
           room: progression_fry_kept); offered = its home may have taken it (it
           restores it from bubbles) - never two */
        if (t->fry_give_k >= 0) {
            if (t->fry_give_sent) t->vis_pending[t->fry_give_k] = false; else progression_fry_kept(t, t->fry_give_k);
            t->fry_give_k = -1;
        }
        tank_link_doors_shut(t); return;
    }
    /* cross-tank fry (phase 5): ask the courting visitor's home for room; hand a fry born for it over.
       Asked again every TANK_LINK_FRY_ASK_S, yes or no (2026-11-03, the cap): a yes is a place its home holds only
       while we keep asking, and a yes not heard again in TANK_LINK_FRY_FRESH_S is not known any more - no fry is born
       for its home on it (progression.c do_arrival). The pair off: its home lets go at once - unless the fry is on
       its way there (its FRY ends the hold) */
    if (t->fry_ask_uid && t->fry_ask_uid != t->fry_partner) {
        if (t->fry_give_k < 0 || t->fry_give_parent != t->fry_ask_uid) link_fry_off(L, t->fry_ask_uid);
        t->fry_ask_uid = 0;
    }
    if (t->fry_home_room == 1 && t->clock - t->fry_room_at > TANK_LINK_FRY_FRESH_S) t->fry_home_room = -1;
    if (t->fry_partner && (!t->fry_asked || t->clock - t->fry_ask_at >= TANK_LINK_FRY_ASK_S)) {
        link_fry_ask(L, t->fry_partner); t->fry_asked = true; t->fry_ask_at = t->clock; t->fry_ask_uid = t->fry_partner;
    }
    if (t->fry_give_k >= 0 && !t->fry_give_sent) {
        int k = t->fry_give_k;
        const fish_t *fry = &t->fish[N_FISH_MAX + k];
        passport_t pp; pp_of(t, &pp, fry, t->vis_home[k], 0);
        link_kin_t kin; memset(&kin, 0, sizeof kin);    /* our parent of it: its home knows it by name */
        uint32_t ours = fry->parent_uid[0] == t->fry_give_parent ? fry->parent_uid[1] : fry->parent_uid[0];
        int ps = home_slot(t, ours);
        if (ps >= 0) {
            const fish_t *p = &t->fish[ps];
            kin.uid = p->uid; memcpy(kin.name, p->name, FISH_NAME_MAX); kin.color = p->color & 0xffffff;
            kin.bold = (uint8_t)(p->bold * 255 + 0.5f); kin.sociable = (uint8_t)(p->sociable * 255 + 0.5f);
        }
        if (link_fry_give(L, t->fry_give_parent, &pp, ps >= 0 ? &kin : NULL)) t->fry_give_sent = true;
        else { link_fry_off(L, t->fry_give_parent); progression_fry_kept(t, k); t->fry_give_k = -1; }   /* (its home lets go of the place) */
    }
    for (int e = 0; e < LINK_EDGES; e++) t->door_open[e] = link_door_open(L, (link_edge_t)e);
    if (gb) for (int e = 0; e < LINK_EDGES; e++) {        /* who is next door: the GIVE modal names it */
        gb->door_peer[e] = t->door_open[e] ? link_neighbor(L, (link_edge_t)e) : 0;
        gb->door_badge[e] = gb->door_peer[e] ? (int8_t)link_peer_badge(L, gb->door_peer[e]) : -1;
    }
    t->link_badge = (int8_t)link_badge(L);
    if (t->deport_uid) { link_deny(L, t->deport_uid, LINK_DEPORT_MS); t->deport_uid = 0; }
    for (int i = 0; i < N_FISH_SLOTS; i++) {
        if (!tank_swims(t, i)) continue;
        fish_t *f = &t->fish[i];
        if (gb && gb->leaving_uid && f->uid == gb->leaving_uid) continue;   /* (a given fish: gift_tick sees it out) */
        if (i < N_FISH_MAX) {                            /* ours */
            if (f->door == DOOR_ASKED && !f->door_sent) {
                passport_t pp; pp_of(t, &pp, f, t->uid, progression_age_s(t, i));
                link_offer(L, (link_edge_t)f->door_edge, &pp, depth_of(f), f->heading, f->speed);
                f->door_sent = true;
            } else if (f->door == DOOR_ASKED && link_accepted(L, f->uid, (link_edge_t)f->door_edge)) {
                f->door = DOOR_PASSING;                  /* back at the glass with a yes it got while elsewhere (the model wavered) */
            } else if (f->door == DOOR_PASSING && beyond(f)) {
                if (link_cross(L, f->uid)) { f->away = true; f->door = DOOR_NONE; f->door_seen = t->clock; progression_settings_changed(); }   /* (saved soon as
                                                     AWAY: a boot from an older save had it home at once, beside its copy next door until the lease ran out) */
                else { f->door = DOOR_NONE; f->door_sent = false; f->door_refused = t->clock + TANK_LINK_REFUSED_S; }   /* too late: it turns back */
                f->door_sent = false;
            }
        } else if (f->door == DOOR_PASSING && beyond(f)) {   /* a visitor through its door, home */
            int k = i - N_FISH_MAX;
            passport_t pp; pp_of(t, &pp, f, t->vis_home[k], t->vis_age[k]);
            link_leaving(L, f->uid, &pp, depth_of(f), f->heading, f->speed);
            collection_left(t, f->uid);
            t->vis_used[k] = false;
        }
    }
}

void tank_link_deport(tank_t *t, int vis_slot) {
    if (!tank_is_visitor(vis_slot) || !t->vis_used[vis_slot - N_FISH_MAX]) return;
    fish_t *f = &t->fish[vis_slot];
    f->homeward = true; f->deported = true; f->hesitate = 0;
    t->deport_uid = f->uid;
}
bool tank_link_event(tank_t *t, link_t *L, const link_ev_t *e) {
    (void)L;
    int i;
    switch (e->type) {
    case LEV_CROSS_OK:
        if ((i = home_slot(t, e->uid)) < 0) return false;
        /* on through - if it still wants that door: asking, or heading back to it (2026-10-05: the model
           changed its mind for a moment between the ask and the yes, explore chose the door again, and
           the yes found it HEADING: it hovered at the glass until the window closed) */
        if ((t->fish[i].door == DOOR_ASKED || t->fish[i].door == DOOR_HEADING) && t->fish[i].door_edge == (int)e->edge) t->fish[i].door = DOOR_PASSING;
        return true;
    case LEV_CROSS_NO:
        if ((i = home_slot(t, e->uid)) < 0) return false;
        { fish_t *f = &t->fish[i];
          if (!f->away && (f->door == DOOR_ASKED || f->door == DOOR_PASSING))
              { f->door = DOOR_NONE; f->door_refused = t->clock + (e->reason == LINK_NO_DEPORTED ? LINK_DEPORT_MS / 1000.0f : e->reason == LINK_NO_FULL ? TANK_LINK_FULL_S : TANK_LINK_REFUSED_S); }
          f->door_sent = false; }
        return true;
    case LEV_VISITOR_IN: {
        int k = -1;
        for (int j = 0; j < N_VISIT_MAX && k < 0; j++) if (!t->vis_used[j] && !t->vis_pending[j]) k = j;
        if (k < 0 || vis_slot(t, e->uid) >= 0 || home_slot(t, e->uid) >= 0) return true;   /* (room() said yes; a clash of IDs: not drawn twice) */
        fish_t *f = &t->fish[N_FISH_MAX + k];
        memset(f, 0, sizeof *f);
        passport_to_fish(&e->pp, f);
        f->goal.id = GOAL_EXPLORE; f->goal.urgency = 3; f->goal.confidence = 1; f->goal.runner_up = GOAL_COUNT;
        f->goal_prev = GOAL_COUNT; f->zone_last = -1; f->ask_age = 99; f->sig = 0xffffffffu;
        f->wander = (float)(e->uid % 628) / 100.0f; f->turn_rate = f->turn_rate > 0 ? f->turn_rate : 2.4f;
        for (int z = 0; z < 6; z++) f->zone_seen[z] = t->clock - 600;   /* every corner of this tank is new to it */
        f->door_seen = t->clock;
        arrive_at(f, e->edge, e->depth, e->speed);
        t->vis_used[k] = true; t->vis_home[k] = e->peer; t->vis_age[k] = e->pp.age_s; t->vis_since[k] = t->clock;
        memcpy(t->vis_home_name[k], e->pp.home_name, sizeof t->vis_home_name[k]);
        t->vis_badge[k] = L ? (int8_t)link_peer_badge(L, e->peer) : -1;
        collection_visit(e->uid);
        kin_learn(e->uid, f->name, f->color, f->bold, f->sociable);   /* (a family line can name it) */
        return true; }
    case LEV_VISITOR_POOF:
        if ((i = vis_slot(t, e->uid)) < 0) return false;
        collection_left(t, e->uid);
        tank_poof(t, t->fish[i].x, t->fish[i].y);
        t->vis_used[i - N_FISH_MAX] = false;
        return true;
    case LEV_VISITOR_HOMEWARD:
        if ((i = vis_slot(t, e->uid)) < 0) return false;
        t->fish[i].homeward = true;                      /* (tank.c steers it to the door it came in by) */
        return true;
    case LEV_FISH_BACK:
        if ((i = home_slot(t, e->uid)) < 0) return false;
        if (!t->fish[i].away) return true;
        welcome_home(t, i, &e->pp);
        arrive_at(&t->fish[i], e->edge, e->depth, e->speed);
        progression_fry_home(t, i);                      /* (a fry born next door: its birth flow now) */
        return true;
    case LEV_FISH_RESTORE:
        if ((i = home_slot(t, e->uid)) < 0) return false;
        if (!t->fish[i].away) return true;
        { fish_t *f = &t->fish[i];
          int de = f->door_edge;
          f->x = t->door_open[de] ? glass_x(de) + (de == LINK_LEFT ? 40 : -40) : TANK_W * 0.5f;   /* by its door, or the middle */
          f->y = DOOR_Y; tank_glass_clamp(&f->x, &f->y, 30);
          welcome_home(t, i, &e->pp);
          f->door = DOOR_NONE;
          tank_poof(t, f->x, f->y);
          progression_fry_home(t, i); }
        return true;
    /* cross-tank fry (phase 5) */
    case LEV_FRY_ROOM:
        if (e->uid == t->fry_partner) { t->fry_home_room = (int8_t)(e->reason ? 1 : 0); t->fry_room_at = t->clock; }
        return true;
    case LEV_FRY_IN:                                     /* home: ours, born next door - into the place held for it */
        if (e->kin.uid) kin_learn(e->kin.uid, e->kin.name, e->kin.color, e->kin.bold / 255.0f, e->kin.sociable / 255.0f);
        for (int h = 0; h < TANK_FRY_HOLDS; h++) if (t->fry_hold[h].peer == e->peer) t->fry_hold[h].peer = t->fry_hold[h].parent = 0;
        if (home_slot(t, e->uid) >= 0) return true;
        if (progression_fry_adopt(t, &e->pp, e->edge) < 0) printf("tank_link: FRY_IN %08x: no room (%d fish held)\n", (unsigned)e->uid, progression_fish_held(t));
        return true;
    case LEV_FRY_OUT: {                                  /* host: its home took it - off it goes, its visiting parent after it */
        int k = t->fry_give_k;
        if (k < 0 || !t->vis_pending[k] || t->fish[N_FISH_MAX + k].uid != e->uid) return false;
        fish_t *f = &t->fish[N_FISH_MAX + k];
        t->vis_pending[k] = false; t->vis_used[k] = true; t->vis_since[k] = t->clock;
        f->homeward = f->bolt_home = true; f->door = DOOR_NONE; f->away = false;
        t->fry_give_k = -1; t->fry_give_sent = false;
        int p = vis_slot(t, t->fry_give_parent);
        if (p >= 0) { t->fish[p].homeward = true; t->fish[p].follow_uid = e->uid; }   /* (Strato: the parent from next door follows the baby home) */
        return true; }
    case LEV_FRY_KEEP:
        if (t->fry_give_k >= 0) { progression_fry_kept(t, t->fry_give_k); t->fry_give_k = -1; t->fry_give_sent = false; }
        return true;
    case LEV_FRY_LOST:
        if (t->fry_give_k >= 0) { t->vis_pending[t->fry_give_k] = false; t->fry_give_k = -1; t->fry_give_sent = false; }
        return true;
    case LEV_LINKED: {                                   /* something new over there */
        for (int j = 0; j < t->n_fish; j++) if (!t->fish[j].away) t->fish[j].curiosity = clampf_(t->fish[j].curiosity + TANK_LINK_CURIOUS, 0, 10);
        gift_book_t *g = gift_of(t);                     /* a gift we were in the middle of with that tank */
        if (g && L) {
            for (int k = 0; k < g->n_given; k++) if (g->given[k].peer == e->peer) link_give_resume_out(L, e->peer, g->given[k].uid, g->given[k].nonce, &g->given[k].pp);
            for (int k = 0; k < g->n_arr; k++) if (g->arriving[k].peer == e->peer) link_give_resume_in(L, e->peer, g->arriving[k].uid, g->arriving[k].nonce, &g->arriving[k].pp);
        }
        return false; }
    /* GIVE (phase 6) */
    case LEV_GIVE_ASK: {                                 /* a gift offered to us: the question on our glass (one at a time) */
        gift_book_t *g = gift_of(t);
        if (!g || g->ask.on) { if (L) link_give_refuse(L, e->uid, e->nonce, LINK_GIVE_NO_OTHER); return true; }   /* (one card at a time:
                                                                   CANNOT BE GIVEN RIGHT NOW, never a false NO THANKS - 11-02 sweep) */
        g->ask.on = true; g->ask.uid = e->uid; g->ask.nonce = e->nonce; g->ask.peer = e->peer; g->ask.pp = e->pp;
        g->ask.badge = L ? (int8_t)link_peer_badge(L, e->peer) : -1; g->ask.since = t->clock; g->ask.answer = 0;
        g->ask.shown = false; g->ask.seen = 0;
        return true; }
    case LEV_GIVE_CANCEL: {                              /* the giver kept it (or the question ran out) */
        gift_book_t *g = gift_of(t);
        if (!g) return true;
        if (g->ask.on && g->ask.uid == e->uid && g->ask.nonce == e->nonce) g->ask.on = false;
        gift_arriving_drop(g, e->uid, e->nonce);
        return true; }
    case LEV_GIVE_TAKEN: {                               /* accepted next door: out it swims - its GIVEN note is saved as it leaves (gift_tick) */
        gift_book_t *g = gift_of(t);
        if (!g) { if (L) link_give_keep(L, e->uid, e->nonce); return true; }
        int i = home_slot(t, e->uid);
        if (g->asking_uid == e->uid) g->asking_uid = 0;
        if (i < 0 || (g->leaving_uid && g->leaving_uid != e->uid) || !progression_keeps_two(t, i)) {   /* (not ours to give now, another gift on its
                                                                     way out, or the last pair would go: it stays) */
            if (L) link_give_keep(L, e->uid, e->nonce);
            gift_said(t, g, e->uid, LINK_GIVE_NO_OTHER);
            return true;
        }
        g->leaving_uid = e->uid; g->leaving_nonce = e->nonce; g->leaving_peer = e->peer; g->leaving_pp = e->pp;
        g->leaving_edge = (int8_t)edge_of(L, e->peer); g->leaving_since = t->clock;
        g->leaving_here = t->fish[i].away;
        if (L) g->peer_badge = (int8_t)link_peer_badge(L, e->peer);
        return true; }
    case LEV_GIVE_NO: {                                  /* it stays ours: the card says why for a while */
        gift_book_t *g = gift_of(t);
        if (!g) return true;
        if (g->asking_uid == e->uid && g->asking_nonce && e->nonce && g->asking_nonce != e->nonce) return true;   /* (a NO for an older ask of
                                                         this fish: the new one still stands, its card says nothing yet - 0.4 rc round two) */
        if (g->asking_uid == e->uid) g->asking_uid = 0;
        if (e->reason != LINK_GIVE_NO_CANCEL) { g->said_uid = e->uid; g->said_reason = e->reason; g->said_until = t->clock + GIFT_SAID_S; }
        return true; }
    case LEV_GIVE_ALIVE: {                               /* the gift is ours: it swims in through the door */
        gift_book_t *g = gift_of(t);
        if (!g) return true;
        int i = home_slot(t, e->uid), vs = vis_slot(t, e->uid);
        if (i < 0 && vs >= 0) {                      /* given where it is: the visitor stays, ours from now on - just where it swims */
            const int k = vs - N_FISH_MAX;
            fish_t was = t->fish[vs];
            passport_t pp; pp_of(t, &pp, &was, t->vis_home[k], t->vis_age[k]);
            if ((i = progression_gift_adopt(t, &pp)) < 0) { printf("tank_link: GIVE_ALIVE %08x: no slot (%d fish)\n", (unsigned)e->uid, t->n_fish); return true; }
            fish_t *f = &t->fish[i];
            f->x = was.x; f->y = was.y; f->heading = was.heading; f->facing = was.facing; f->speed = was.speed; f->target_speed = was.target_speed;
            f->yaw = was.yaw; f->yaw_tail = was.yaw_tail; f->gdepth = was.gdepth; f->pdepth = was.pdepth;
            collection_left(t, e->uid);
            t->vis_used[k] = false;
        } else if (i < 0) {
            if ((i = progression_gift_adopt(t, &e->pp)) < 0) {   /* (no slot at all: the note stays, the giver asks again) */
                printf("tank_link: GIVE_ALIVE %08x: no slot (%d fish)\n", (unsigned)e->uid, t->n_fish); return true; }
            int ed = edge_of(L, e->peer);
            if (ed >= 0) arrive_at(&t->fish[i], ed, 0.5f, 30);
            else { fish_t *f = &t->fish[i]; f->x = TANK_W * 0.5f; f->y = DOOR_Y; tank_glass_clamp(&f->x, &f->y, 30); tank_poof(t, f->x, f->y); }
        }
        gift_received(g, e->uid, e->nonce);
        progression_cap_check(t, "LEV_GIVE_ALIVE");   /* (after the note: the place it held is the fish now) */
        if (L) link_give_alive(L, e->uid, e->nonce);
        return true; }
    case LEV_GIVE_CLOSED: gift_given_drop(gift_of(t), e->uid, e->nonce); return true;
    case LEV_GIVE_RESTORE: {                             /* the other tank never had it: it lives here again, from bubbles */
        gift_book_t *g = gift_of(t);
        if (!g) return true;
        if (home_slot(t, e->uid) < 0) {
            int i = progression_gift_adopt(t, &e->pp);
            if (i < 0) {                                 /* (no slot: the note stays and holds its place - the next meeting asks
                                                            again. Never a fish nobody holds: 11-03 rc) */
                printf("tank_link: GIVE_RESTORE %08x: no slot (%d fish), its note kept\n", (unsigned)e->uid, t->n_fish);
                return true;
            }
            fish_t *f = &t->fish[i]; f->x = TANK_W * 0.5f; f->y = DOOR_Y; tank_glass_clamp(&f->x, &f->y, 30); tank_poof(t, f->x, f->y);
        }
        gift_given_drop(g, e->uid, e->nonce);
        progression_cap_check(t, "LEV_GIVE_RESTORE");
        return true; }
    default:
        return false;
    }
}
