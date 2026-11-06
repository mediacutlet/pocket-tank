/* link.c — the tank link's protocol engine (link.h has the contract and the
 * two promises; docs/tank-link-0.4.md the why). */
#include <string.h>
#include <math.h>
#include "link.h"

/* ---- frames: a 12-byte header, then the type's body. Little-endian. ---- */
#define LINK_MAGIC 0x4b4cu   /* "LK" */
enum {
    FR_BEACON = 1, FR_JOIN, FR_LINKED, FR_UNLINK, FR_OFFER, FR_ACCEPT, FR_REFUSE, FR_GO,
    FR_VISIT, FR_LEASE, FR_RETURN, FR_HOME, FR_RECALL, FR_DROP, FR_GOHOME,
    FR_FRY_ASK, FR_FRY_ROOM, FR_FRY, FR_FRY_OK, FR_FRY_NO,
    FR_GIVE, FR_GIVE_TAKE, FR_GIVE_NO, FR_GIVE_CANCEL, FR_GIVEN, FR_GIVEN_OK, FR_GIVEN_LOST,   /* phase 6 (an older tank drops them: NO ANSWER) */
    FR_TYPES
};
#define HDR 12
enum { BF_MODE = 1, BF_DRIFT = 2 };          /* beacon flags */
enum { RF_POOF = 1 };                        /* return flags */

typedef struct {
    bool used, announced; uint32_t tank, heard; int rssi;   /* announced: a beacon of it has been read (the NEARBY row knows it) */
    uint8_t badge, flags, armed, room, drift_s;
    uint32_t edge_peer[LINK_EDGES];          /* what its own beacon says it is linked to */
} peer_t;

typedef enum { AW_FREE, AW_OFFERING, AW_ACCEPTED, AW_AWAY } aw_state_t;
typedef struct {                             /* one of ours, heading out or next door */
    aw_state_t st; uint32_t uid, peer; uint16_t trip; link_edge_t edge;
    uint32_t t0, sent, visit, lease_sent;
    float depth, heading, speed;
    passport_t pp;                           /* the last we know of it: our backup */
} away_t;

typedef enum { GO_FREE, GO_ASKING, GO_TAKEN, GO_GIVEN } go_state_t;   /* a gift of ours: asking / the tank saving / GIVEN until answered */
typedef struct { go_state_t st; uint32_t uid, nonce, peer, t0, sent; passport_t pp; } give_out_t;
typedef enum { GI_FREE, GI_ASKING, GI_TAKE, GI_ALIVE } gi_state_t;     /* a gift to us: our keeper asked / TAKE until answered / the tank saving */
typedef struct { gi_state_t st; uint32_t uid, nonce, from, t0, sent; passport_t pp; } give_in_t;

typedef enum { VS_FREE, VS_PENDING, VS_ACTIVE, VS_RETURNING } vs_state_t;
typedef struct {                             /* a visitor here */
    vs_state_t st; uint32_t uid, home; uint16_t trip; link_edge_t edge;
    uint32_t t0, lease_until, reported, sent;
    float depth, heading, speed;
    bool poof;
    passport_t pp;
} visit_t;

struct link {
    uint32_t me, now;
    link_host_t host;
    bool mode; uint32_t mode_active;
    uint8_t badge;
    peer_t peer[LINK_PEERS_MAX];
    uint32_t armed_until[LINK_EDGES], join_sent[LINK_EDGES];
    uint32_t edge_peer[LINK_EDGES], mismatch_since[LINK_EDGES];
    uint32_t asking[LINK_EDGES]; uint8_t asking_n[LINK_EDGES];
    bool moving; uint32_t move_since, still_since;
    bool drifting; uint32_t drift_since; bool homeward; uint32_t homeward_at;
    uint32_t settle_until;                   /* a door just opened: moving is settling, not drifting (LINK_SETTLE_MS) */
    bool notice;
    uint16_t trip;
    uint32_t beacon_at;
    away_t away[LINK_AWAY_MAX];
    visit_t visit[LINK_VISIT_MAX + 1];       /* + one place held for an accepted OFFER */
    struct { uint32_t uid, until; } deny[4];   /* visitors DEPORTed: refused at our door until then */
    struct {                                 /* a fry born here, its home's, being handed over */
        bool on; uint32_t parent, home, t0, sent; link_edge_t edge; passport_t pp; link_kin_t kin;
    } fry;
    give_out_t gout[LINK_GIVE_MAX];          /* GIVE: ours, going */
    give_in_t  gin[LINK_GIVE_MAX];           /* ... theirs, coming */
    link_ev_t evq[LINK_EVQ]; int ev_head, ev_n;
    uint32_t ev_lost;
};

int link_size(void) { return (int)sizeof(link_t); }

static link_edge_t opp(link_edge_t e) { return e == LINK_LEFT ? LINK_RIGHT : LINK_LEFT; }
static bool after(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

/* ---- events ---- */
static link_ev_t *ev_push(link_t *L, link_ev_type_t type) {
    if (L->ev_n >= LINK_EVQ) { L->ev_lost++; return NULL; }
    link_ev_t *e = &L->evq[(L->ev_head + L->ev_n++) % LINK_EVQ];
    memset(e, 0, sizeof *e); e->type = type;
    return e;
}
bool link_poll(link_t *L, link_ev_t *ev) {
    if (!L->ev_n) return false;
    *ev = L->evq[L->ev_head]; L->ev_head = (L->ev_head + 1) % LINK_EVQ; L->ev_n--;
    return true;
}

/* ---- writing and reading frames ---- */
typedef struct { uint8_t b[LINK_FRAME_MAX]; int n; } wr_t;
static void w8(wr_t *w, uint32_t v) { if (w->n < LINK_FRAME_MAX) w->b[w->n++] = (uint8_t)v; }
static void w16(wr_t *w, uint32_t v) { w8(w, v); w8(w, v >> 8); }
static void w32(wr_t *w, uint32_t v) { w16(w, v & 0xffff); w16(w, v >> 16); }
static void wpp(wr_t *w, const passport_t *pp) {
    uint8_t buf[PASSPORT_MAX_BYTES]; int n = passport_write(pp, buf, sizeof buf);
    w8(w, (uint32_t)n); for (int i = 0; i < n; i++) w8(w, buf[i]);
}
static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static void wmotion(wr_t *w, float depth, float heading, float speed) {
    depth = depth < 0 ? 0 : depth > 1 ? 1 : depth;
    speed = speed < 0 ? 0 : speed > 6000 ? 6000 : speed;
    w16(w, (uint32_t)lroundf(depth * 65535.0f));
    w16(w, (uint16_t)(int16_t)lroundf(wrap_pi(heading) * 10000.0f));
    w16(w, (uint32_t)lroundf(speed * 10.0f));
}
static void hdr(link_t *L, wr_t *w, int type, uint32_t to) {
    w->n = 0; w16(w, LINK_MAGIC); w8(w, LINK_PROTO); w8(w, (uint32_t)type); w32(w, L->me); w32(w, to);
}
static void tx(link_t *L, wr_t *w, uint32_t to) { if (L->host.send) L->host.send(L->host.ctx, to, w->b, w->n); }

typedef struct { const uint8_t *b; int n, p; bool bad; } rd_t;
static uint32_t r8(rd_t *r) { if (r->p + 1 > r->n) { r->bad = true; return 0; } return r->b[r->p++]; }
static uint32_t r16(rd_t *r) { uint32_t a = r8(r); return a | r8(r) << 8; }
static uint32_t r32(rd_t *r) { uint32_t a = r16(r); return a | r16(r) << 16; }
static bool rpp(rd_t *r, passport_t *pp) {
    int n = (int)r8(r);
    if (r->bad || r->p + n > r->n) { r->bad = true; return false; }
    bool ok = passport_read(pp, r->b + r->p, n); r->p += n;
    return ok;
}
static void rmotion(rd_t *r, float *depth, float *heading, float *speed) {
    *depth = (float)r16(r) / 65535.0f;
    *heading = (float)(int16_t)r16(r) / 10000.0f;
    *speed = (float)r16(r) / 10.0f;
}

/* ---- tables ---- */
static peer_t *peer_find(link_t *L, uint32_t tank) {
    for (int i = 0; i < LINK_PEERS_MAX; i++) if (L->peer[i].used && L->peer[i].tank == tank) return &L->peer[i];
    return NULL;
}
static bool linked_to(const link_t *L, uint32_t tank) { return tank && (L->edge_peer[0] == tank || L->edge_peer[1] == tank); }
static peer_t *peer_get(link_t *L, uint32_t tank) {
    peer_t *p = peer_find(L, tank);
    if (p) return p;
    int oldest = -1;
    for (int i = 0; i < LINK_PEERS_MAX; i++) {
        if (!L->peer[i].used) { oldest = i; break; }
        if (linked_to(L, L->peer[i].tank)) continue;
        if (oldest < 0 || !after(L->peer[i].heard, L->peer[oldest].heard)) oldest = i;
    }
    if (oldest < 0) return NULL;
    p = &L->peer[oldest]; memset(p, 0, sizeof *p); p->used = true; p->tank = tank; p->heard = L->now;
    return p;
}
static bool fresh(const link_t *L, const peer_t *p) { return p && p->used && !after(L->now, p->heard + LINK_PEER_FRESH_MS); }
static away_t *away_find(link_t *L, uint32_t uid) {
    for (int i = 0; i < LINK_AWAY_MAX; i++) if (L->away[i].st != AW_FREE && L->away[i].uid == uid) return &L->away[i];
    return NULL;
}
static give_out_t *gout_find(link_t *L, uint32_t uid);   /* GIVE, below */
static visit_t *visit_find(link_t *L, uint32_t uid) {
    for (int i = 0; i <= LINK_VISIT_MAX; i++) if (L->visit[i].st != VS_FREE && L->visit[i].uid == uid) return &L->visit[i];
    return NULL;
}
static int visitors_held(const link_t *L) {
    int n = 0;
    for (int i = 0; i <= LINK_VISIT_MAX; i++) n += L->visit[i].st == VS_PENDING || L->visit[i].st == VS_ACTIVE;
    return n;
}

/* places given that the host cannot see yet: an OFFER accepted (PENDING, its GO still to come) or a GO taken whose
   LEV_VISITOR_IN the tank has not polled. host.room counts only the visitors it draws, so two offers at once each found
   the one place free and both came in (0.4.1 audit, link F2a: 4 at home + 2 visitors = 6 under a 5-fish rule) */
int link_visitors_coming(const link_t *L) {
    int n = 0;
    for (int i = 0; i <= LINK_VISIT_MAX; i++) n += L->visit[i].st == VS_PENDING;
    for (int q = 0; q < L->ev_n; q++) n += L->evq[(L->ev_head + q) % LINK_EVQ].type == LEV_VISITOR_IN;
    return n;
}
static int room_left(const link_t *L) {       /* visitor places free: the host's word, less what it cannot see yet */
    int room = L->host.room ? L->host.room(L->host.ctx) - link_visitors_coming(L) : LINK_VISIT_MAX - visitors_held(L);
    return room < 0 ? 0 : room;
}

/* ---- small senders ---- */
static void send_fish(link_t *L, int type, uint32_t to, uint32_t uid, uint16_t trip) {
    wr_t w; hdr(L, &w, type, to); w32(&w, uid); w16(&w, trip); tx(L, &w, to);
}
static void send_edge(link_t *L, int type, uint32_t to, link_edge_t e, uint8_t why) {
    wr_t w; hdr(L, &w, type, to); w8(&w, e); w8(&w, why); tx(L, &w, to);
}
static void send_beacon(link_t *L) {
    wr_t w; hdr(L, &w, FR_BEACON, 0);
    uint8_t armed = 0;
    for (int e = 0; e < LINK_EDGES; e++) if (L->armed_until[e] && !after(L->now, L->armed_until[e]) && !L->edge_peer[e]) armed |= (uint8_t)(1u << e);
    int room = L->host.room ? room_left(L) : 0;
    room = room > 255 ? 255 : room;
    uint32_t ds = L->drifting ? (L->now - L->drift_since) / 1000 : 0;
    w8(&w, L->badge); w8(&w, (L->mode ? BF_MODE : 0) | (L->drifting ? BF_DRIFT : 0)); w8(&w, armed);
    w8(&w, (uint32_t)room); w8(&w, ds > 255 ? 255 : ds);
    w32(&w, L->edge_peer[LINK_LEFT]); w32(&w, L->edge_peer[LINK_RIGHT]);
    tx(L, &w, 0);
}
static void send_offer(link_t *L, const away_t *a) {
    wr_t w; hdr(L, &w, FR_OFFER, a->peer);
    w32(&w, a->uid); w16(&w, a->trip); w8(&w, a->edge); wmotion(&w, a->depth, a->heading, a->speed); wpp(&w, &a->pp);
    tx(L, &w, a->peer);
}
static void send_visit(link_t *L, const visit_t *v) {
    wr_t w; hdr(L, &w, FR_VISIT, v->home); w32(&w, v->uid); w16(&w, v->trip); wpp(&w, &v->pp); tx(L, &w, v->home);
}
static void send_return(link_t *L, const visit_t *v) {
    wr_t w; hdr(L, &w, FR_RETURN, v->home);
    w32(&w, v->uid); w16(&w, v->trip); w8(&w, v->poof ? RF_POOF : 0); w8(&w, v->edge);
    wmotion(&w, v->depth, v->heading, v->speed); wpp(&w, &v->pp);
    tx(L, &w, v->home);
}

/* ---- the visitor side ---- */
static void visitor_fresh_pp(link_t *L, visit_t *v) {   /* the host tank's latest of it, kept as ours */
    passport_t pp;
    if (L->host.visitor && L->host.visitor(L->host.ctx, v->uid, &pp)) { v->pp = pp; v->pp.home_tank = v->home; v->pp.uid = v->uid; }
}
/* a visitor turns to bubbles here; home hears RETURN (poof) with its last state */
static void visitor_poof(link_t *L, visit_t *v) {
    if (v->st == VS_PENDING) { v->st = VS_FREE; return; }
    if (v->st != VS_ACTIVE) return;
    visitor_fresh_pp(L, v);
    link_ev_t *e = ev_push(L, LEV_VISITOR_POOF); if (e) { e->uid = v->uid; e->peer = v->home; }
    v->st = VS_RETURNING; v->poof = true; v->t0 = L->now; v->sent = L->now;
    send_return(L, v);
}

/* ---- edges ---- */
static void unlink_edge(link_t *L, link_edge_t e, link_why_t why, bool tell) {
    uint32_t p = L->edge_peer[e];
    if (!p) return;
    L->edge_peer[e] = 0; L->mismatch_since[e] = 0; L->armed_until[e] = 0;
    if (tell) send_edge(L, FR_UNLINK, p, e, (uint8_t)why);
    if (!linked_to(L, p)) {                 /* (a tank is only ever at one edge) */
        for (int i = 0; i <= LINK_VISIT_MAX; i++) if (L->visit[i].st != VS_FREE && L->visit[i].home == p) visitor_poof(L, &L->visit[i]);
        for (int i = 0; i < LINK_AWAY_MAX; i++) {
            away_t *a = &L->away[i];
            if (a->st == AW_FREE || a->peer != p) continue;
            if (a->st == AW_AWAY) send_fish(L, FR_RECALL, p, a->uid, a->trip);
            else { link_ev_t *ev = ev_push(L, LEV_CROSS_NO); if (ev) ev->uid = a->uid; a->st = AW_FREE; }
        }
    }
    link_ev_t *ev = ev_push(L, LEV_UNLINKED); if (ev) { ev->edge = e; ev->peer = p; ev->reason = (uint8_t)why; }
}
static void link_edge_to(link_t *L, link_edge_t e, uint32_t p) {
    L->edge_peer[e] = p; L->armed_until[e] = 0; L->mismatch_since[e] = 0; L->mode_active = L->now;
    L->settle_until = L->now + LINK_SETTLE_MS;
    peer_t *pr = peer_find(L, p); if (pr) pr->edge_peer[opp(e)] = L->me;   /* until its next beacon says so */
    link_ev_t *ev = ev_push(L, LEV_LINKED); if (ev) { ev->edge = e; ev->peer = p; }
}
static bool armed(const link_t *L, link_edge_t e) { return L->armed_until[e] && !after(L->now, L->armed_until[e]); }
/* the tanks that could join this edge now: fresh, in link mode, asking with
 * the facing edge, free there, not already linked to us */
static int candidates(link_t *L, link_edge_t e, uint32_t *one) {
    int n = 0; *one = 0;
    for (int i = 0; i < LINK_PEERS_MAX; i++) {
        peer_t *p = &L->peer[i];
        /* (one that already counts itself linked to US there is still a candidate:
           our LINKED reply was lost, and its JOINs must keep coming to get another) */
        if (!fresh(L, p) || !(p->flags & BF_MODE) || !(p->armed & (1u << opp(e))) || linked_to(L, p->tank)) continue;
        if (p->edge_peer[opp(e)] && p->edge_peer[opp(e)] != L->me) continue;
        n++; *one = p->tank;
    }
    return n;
}
static void pick_badge(link_t *L, int avoid) {
    uint8_t used = 0;
    for (int i = 0; i < LINK_PEERS_MAX; i++) if (fresh(L, &L->peer[i]) && L->peer[i].badge < LINK_BADGES) used |= (uint8_t)(1u << L->peer[i].badge);
    if (avoid >= 0) used |= (uint8_t)(1u << avoid);
    for (int b = 0; b < LINK_BADGES; b++) if (!(used & (1u << b))) { L->badge = (uint8_t)b; return; }
    L->badge = (uint8_t)(L->me % LINK_BADGES);
}

/* ---- public: setup and the keeper ---- */
void link_init(link_t *L, uint32_t my_tank, const link_host_t *host, uint32_t trip_seed, uint32_t now_ms) {
    memset(L, 0, sizeof *L);
    L->me = my_tank; if (host) L->host = *host;
    L->now = now_ms; L->trip = (uint16_t)(trip_seed ^ (trip_seed >> 16));
    L->badge = (uint8_t)(my_tank % LINK_BADGES);
}
void link_mode(link_t *L, bool on) {
    if (on && !L->mode) { pick_badge(L, -1); L->beacon_at = L->now; }
    L->mode = on; L->mode_active = L->now;
    if (!on) L->armed_until[0] = L->armed_until[1] = 0;
}
bool link_in_mode(const link_t *L) { return L->mode; }
void link_arm(link_t *L, link_edge_t e) {
    if (e >= LINK_EDGES || !L->mode || L->edge_peer[e]) return;
    L->armed_until[e] = L->now + LINK_ARM_MS; L->mode_active = L->now;
    L->beacon_at = L->now;                   /* tell everyone at once */
}
void link_close(link_t *L, link_edge_t e) { if (e < LINK_EDGES) unlink_edge(L, e, LINK_WHY_KEEPER, true); }
void link_moving(link_t *L, bool moving) { L->moving = moving; }
void link_sleep(link_t *L) {
    for (int e = 0; e < LINK_EDGES; e++) unlink_edge(L, (link_edge_t)e, LINK_WHY_SLEEP, true);
    link_mode(L, false);
}

/* ---- public: the fish ---- */
bool link_drifting(const link_t *L) { return L->notice; }
/* a door paused by a drift: ours moving pauses both; a neighbor's pauses only the door it is at (the 11-02 sweep: a
   middle tank paused BOTH doors for one neighbor lifted - the other neighbor's fish stopped for nothing) */
static bool tank_paused(const link_t *L, uint32_t tank) {
    if (L->drifting) return true;
    const peer_t *p = tank ? peer_find((link_t *)L, tank) : NULL;
    return fresh(L, p) && (p->flags & BF_DRIFT);
}
bool link_door_open(const link_t *L, link_edge_t e) { return e < LINK_EDGES && L->edge_peer[e] && !tank_paused(L, L->edge_peer[e]); }
uint32_t link_neighbor(const link_t *L, link_edge_t e) { return e < LINK_EDGES ? L->edge_peer[e] : 0; }
void link_offer(link_t *L, link_edge_t e, const passport_t *pp, float depth, float heading, float speed) {
    away_t *a = NULL;
    if (link_door_open(L, e) && !away_find(L, pp->uid) && !link_give_busy(L, pp->uid))   /* (a fish being given stays home) */
        for (int i = 0; i < LINK_AWAY_MAX && !a; i++) if (L->away[i].st == AW_FREE) a = &L->away[i];
    if (!a) { link_ev_t *ev = ev_push(L, LEV_CROSS_NO); if (ev) ev->uid = pp->uid; return; }
    memset(a, 0, sizeof *a);
    a->st = AW_OFFERING; a->uid = pp->uid; a->peer = L->edge_peer[e]; a->trip = ++L->trip; a->edge = e;
    a->t0 = a->sent = L->now; a->depth = depth; a->heading = heading; a->speed = speed;
    a->pp = *pp; a->pp.home_tank = L->me;
    send_offer(L, a);
}
bool link_cross(link_t *L, uint32_t uid) {
    away_t *a = away_find(L, uid);
    if (!a || a->st != AW_ACCEPTED || after(L->now, a->t0 + LINK_CROSS_MS) || !link_door_open(L, a->edge) || L->edge_peer[a->edge] != a->peer) {
        if (a && a->st == AW_ACCEPTED) a->st = AW_FREE;
        return false;
    }
    a->st = AW_AWAY; a->t0 = a->sent = L->now; a->visit = 0; a->lease_sent = 0;
    send_fish(L, FR_GO, a->peer, a->uid, a->trip);
    return true;
}
void link_leaving(link_t *L, uint32_t uid, const passport_t *pp, float depth, float heading, float speed) {
    visit_t *v = visit_find(L, uid);
    if (!v || v->st != VS_ACTIVE) return;
    if (pp) { v->pp = *pp; v->pp.home_tank = v->home; v->pp.uid = v->uid; }
    v->st = VS_RETURNING; v->poof = false; v->t0 = v->sent = L->now;
    v->depth = depth; v->heading = heading; v->speed = speed;
    send_return(L, v);
}
bool link_accepted(const link_t *L, uint32_t uid, link_edge_t e) {
    for (int i = 0; i < LINK_AWAY_MAX; i++) {
        const away_t *a = &L->away[i];
        if (a->st == AW_ACCEPTED && a->uid == uid) return a->edge == e && !after(L->now, a->t0 + LINK_CROSS_MS);
    }
    return false;
}
/* ---- cross-tank fry ---- */
static void send_fry(link_t *L) {
    wr_t w; hdr(L, &w, FR_FRY, L->fry.home); w32(&w, L->fry.parent); w8(&w, L->fry.edge); wpp(&w, &L->fry.pp);
    if (L->fry.kin.uid) {                    /* (appended 2026-10-06: an older reader stops before it) */
        const link_kin_t *k = &L->fry.kin;
        w32(&w, k->uid); for (int i = 0; i < FISH_NAME_MAX; i++) w8(&w, (uint8_t)k->name[i]);
        w8(&w, k->color >> 16); w8(&w, k->color >> 8); w8(&w, k->color); w8(&w, k->bold); w8(&w, k->sociable);
    }
    tx(L, &w, L->fry.home);
}
/* FRY_ASK: uid + trip, then a flag byte (appended 2026-11-03: 1 = the pair is off, let go of the place - no answer).
   An older reader stops before it: it answers ROOM, which the host no longer asks for, and holds nothing anyway */
#define FRY_ASK_HOLD 0                        /* FR_FRY_ASK's flag: an ask (a yes holds the place) */
#define FRY_ASK_OFF  1                        /* ... the pair is off: let go */
#define FRY_ASK_PEEK 2                        /* ... (0.4.1 round 2) room or not, nothing held - a home before it reads an ask */
static void send_fry_ask(link_t *L, const visit_t *v, int flag) {
    wr_t w; hdr(L, &w, FR_FRY_ASK, v->home); w32(&w, v->uid); w16(&w, v->trip); w8(&w, (uint32_t)flag); tx(L, &w, v->home);
}
void link_fry_ask(link_t *L, uint32_t parent_uid) {
    visit_t *v = visit_find(L, parent_uid);
    if (!v || v->st != VS_ACTIVE) { link_ev_t *ev = ev_push(L, LEV_FRY_ROOM); if (ev) { ev->uid = parent_uid; ev->reason = 0; } return; }
    send_fry_ask(L, v, FRY_ASK_HOLD);
}
void link_fry_off(link_t *L, uint32_t parent_uid) {
    visit_t *v = visit_find(L, parent_uid);
    if (v && v->st == VS_ACTIVE) send_fry_ask(L, v, FRY_ASK_OFF);   /* (gone home: its home lets go by itself) */
}
void link_fry_peek(link_t *L, uint32_t parent_uid) {
    visit_t *v = visit_find(L, parent_uid);
    if (!v || v->st != VS_ACTIVE) { link_ev_t *ev = ev_push(L, LEV_FRY_ROOM); if (ev) { ev->uid = parent_uid; ev->reason = 0; } return; }
    send_fry_ask(L, v, FRY_ASK_PEEK);
    send_fry_ask(L, v, FRY_ASK_OFF);          /* (a home older than the peek took it for an ask and held: let go at once - for a home
                                                 that only peeked a no-op, fry_hold(off) drops a hold of this tank and parent only) */
}
void link_deny(link_t *L, uint32_t uid, uint32_t ms) {
    int w = 0;
    for (int i = 0; i < 4; i++) {
        if (L->deny[i].uid == uid) { w = i; break; }
        if (!L->deny[i].uid || after(L->now, L->deny[i].until)) w = i;
    }
    L->deny[w].uid = uid; L->deny[w].until = L->now + ms;
}
static bool denied(const link_t *L, uint32_t uid) {
    for (int i = 0; i < 4; i++) if (L->deny[i].uid == uid && !after(L->now, L->deny[i].until)) return true;
    return false;
}
bool link_fry_give(link_t *L, uint32_t parent_uid, const passport_t *fry, const link_kin_t *other) {
    visit_t *v = visit_find(L, parent_uid);
    if (L->fry.on || !v || v->st != VS_ACTIVE || !linked_to(L, v->home) || tank_paused(L, v->home) || visit_find(L, fry->uid)) return false;
    int free_n = 0;
    for (int i = 0; i <= LINK_VISIT_MAX; i++) free_n += L->visit[i].st == VS_FREE;
    if (!free_n) return false;               /* (the fry needs a place here while it swims home) */
    L->fry.on = true; L->fry.parent = parent_uid; L->fry.home = v->home; L->fry.edge = v->edge;
    L->fry.pp = *fry; L->fry.pp.home_tank = v->home; L->fry.t0 = L->fry.sent = L->now;
    if (other) L->fry.kin = *other; else memset(&L->fry.kin, 0, sizeof L->fry.kin);
    send_fry(L);
    return true;
}
bool link_is_away(const link_t *L, uint32_t uid) {
    for (int i = 0; i < LINK_AWAY_MAX; i++) if (L->away[i].st == AW_AWAY && L->away[i].uid == uid) return true;
    return false;
}
bool link_hosting(const link_t *L, uint32_t uid) {
    for (int i = 0; i <= LINK_VISIT_MAX; i++) if (L->visit[i].st == VS_ACTIVE && L->visit[i].uid == uid) return true;
    return false;
}
int link_badge(const link_t *L) { return L->badge; }
int link_peer_badge(const link_t *L, uint32_t tank) {
    for (int i = 0; i < LINK_PEERS_MAX; i++) if (L->peer[i].used && L->peer[i].tank == tank) return L->peer[i].badge;
    return -1;
}
uint32_t link_events_lost(const link_t *L) { return L->ev_lost; }

void link_view(const link_t *L, link_view_t *v) {
    memset(v, 0, sizeof *v);
    v->badge = L->badge;
    for (int e = 0; e < LINK_EDGES; e++) {
        if (L->edge_peer[e]) {
            const peer_t *p = NULL;
            for (int i = 0; i < LINK_PEERS_MAX && !p; i++) if (L->peer[i].used && L->peer[i].tank == L->edge_peer[e]) p = &L->peer[i];
            v->edge[e].state = LV_OPEN; v->edge[e].badge = p ? p->badge : 0;
            continue;
        }
        /* who asks here: the tanks armed at the facing edge (link.c candidates) */
        int n = 0;
        for (int i = 0; i < LINK_PEERS_MAX; i++) {
            const peer_t *p = &L->peer[i];
            if (!fresh(L, p) || !(p->flags & BF_MODE) || !(p->armed & (1u << opp((link_edge_t)e))) || linked_to(L, p->tank)) continue;
            if (p->edge_peer[opp((link_edge_t)e)] && p->edge_peer[opp((link_edge_t)e)] != L->me) continue;
            if (n == 0) v->edge[e].badge = p->badge; else if (n == 1) v->edge[e].badge2 = p->badge;
            n++;
        }
        v->edge[e].state = n >= 2 ? LV_TWO : n == 1 ? LV_ASKED : armed(L, (link_edge_t)e) ? LV_ARMED : LV_FREE;
    }
    for (int i = 0; i < LINK_PEERS_MAX; i++)
        if (fresh(L, &L->peer[i]) && (L->peer[i].flags & BF_MODE) && v->n_near < LINK_PEERS_MAX) v->near_badge[v->n_near++] = L->peer[i].badge;
    v->drifting = L->notice;
    /* the phase and its time left: ours from our clock; a neighbor's from the
       seconds its beacon says it has been drifting */
    uint32_t ms = 0;
    if (L->drifting) ms = L->now - L->drift_since;
    else for (int e = 0; e < LINK_EDGES; e++) {
        const peer_t *p = NULL;
        for (int i = 0; i < LINK_PEERS_MAX && !p; i++) if (L->edge_peer[e] && L->peer[i].used && L->peer[i].tank == L->edge_peer[e]) p = &L->peer[i];
        if (p && fresh(L, p) && (p->flags & BF_DRIFT) && p->drift_s * 1000u > ms) ms = p->drift_s * 1000u;
    }
    v->drift_homeward = L->notice && (L->drifting ? L->homeward : ms >= LINK_DRIFT_GRACE_MS);
    if (!L->notice) v->drift_ms_left = 0;
    else if (!v->drift_homeward) v->drift_ms_left = ms < LINK_DRIFT_GRACE_MS ? LINK_DRIFT_GRACE_MS - ms : 0;
    else { uint32_t h = L->drifting ? L->now - L->homeward_at : ms - LINK_DRIFT_GRACE_MS; v->drift_ms_left = h < LINK_HOMEWARD_MS ? LINK_HOMEWARD_MS - h : 0; }
}
int link_peers(const link_t *L, link_peer_info_t *out, int max) {
    int n = 0;
    for (int i = 0; i < LINK_PEERS_MAX && n < max; i++) {
        const peer_t *p = &L->peer[i];
        if (!fresh(L, p) || !(p->flags & BF_MODE)) continue;
        out[n].tank = p->tank; out[n].badge = p->badge; out[n].link_mode = true;
        out[n].drifting = (p->flags & BF_DRIFT) != 0; out[n].rssi = p->rssi; n++;
    }
    return n;
}

/* ---- receiving ---- */
static void rx_beacon(link_t *L, peer_t *p, rd_t *r) {
    uint8_t badge = (uint8_t)r8(r), flags = (uint8_t)r8(r), arm = (uint8_t)r8(r), room = (uint8_t)r8(r), ds = (uint8_t)r8(r);
    uint32_t le = r32(r), re = r32(r);
    if (r->bad) return;
    /* (a first beacon used to be guessed from all-zero fields - a circle with
       link mode off and nothing armed looked new on every beacon: a NEARBY a
       second, 2026-10-05) */
    bool was_mode = (p->flags & BF_MODE) != 0, was_new = !p->announced;
    p->announced = true;
    p->badge = badge < LINK_BADGES ? badge : 0; p->flags = flags & (BF_MODE | BF_DRIFT); p->armed = arm & 3;
    p->room = room; p->drift_s = ds; p->edge_peer[LINK_LEFT] = le; p->edge_peer[LINK_RIGHT] = re;
    /* two tanks the same badge: the one in link mode picks again - always, when
       the other is not in link mode (it is showing its badge as somebody's
       neighbor already), else the higher tank ID (2026-10-05: a watch opening
       link mode beside a linked watch both showed the circle) */
    if (L->mode && p->badge == L->badge && (!(p->flags & BF_MODE) || p->tank < L->me)) pick_badge(L, p->badge);
    if (was_new || was_mode != ((p->flags & BF_MODE) != 0)) ev_push(L, LEV_NEARBY);
}
static void rx_join(link_t *L, uint32_t from, link_edge_t theirs) {
    link_edge_t e = opp(theirs);
    if (L->edge_peer[e] == from) { send_edge(L, FR_LINKED, from, e, 0); return; }
    uint32_t one;
    if (L->edge_peer[e] || !armed(L, e) || linked_to(L, from) || candidates(L, e, &one) != 1 || one != from) return;
    link_edge_to(L, e, from);
    send_edge(L, FR_LINKED, from, e, 0);
}
static void rx_linked(link_t *L, uint32_t from, link_edge_t theirs) {
    link_edge_t e = opp(theirs);
    if (L->edge_peer[e] == from || L->edge_peer[e] || linked_to(L, from)) return;
    if (!armed(L, e) && after(L->now, L->join_sent[e] + 2 * LINK_JOIN_RESEND_MS + LINK_ARM_MS)) return;
    if (!L->join_sent[e]) return;            /* we never asked it */
    link_edge_to(L, e, from);
}
static void rx_offer(link_t *L, uint32_t from, rd_t *r) {
    uint32_t uid = r32(r); uint16_t trip = (uint16_t)r16(r); uint32_t edge = r8(r);
    float depth, heading, speed; rmotion(r, &depth, &heading, &speed);
    passport_t pp; bool ok = rpp(r, &pp);
    if (r->bad || edge >= LINK_EDGES) return;
    link_edge_t e = opp((link_edge_t)edge);
    visit_t *v = visit_find(L, uid);
    if (v && v->trip == trip) { if (v->st == VS_PENDING) send_fish(L, FR_ACCEPT, from, uid, trip); return; }
    bool yes = ok && pp.uid == uid && pp.home_tank == from && L->edge_peer[e] == from && !tank_paused(L, from) && !denied(L, uid)
            && (!v || v->st == VS_RETURNING) && visitors_held(L) < LINK_VISIT_MAX
            && room_left(L) > 0;                 /* (counting the places given and not yet drawn: two offers at once) */
    visit_t *slot = NULL;
    if (yes) for (int i = 0; i <= LINK_VISIT_MAX && !slot; i++) if (L->visit[i].st == VS_FREE) slot = &L->visit[i];
    if (!slot) {                             /* no - and why: a full tank is worth waiting out longer than a pause */
        int why = denied(L, uid) ? LINK_NO_DEPORTED : tank_paused(L, from) ? LINK_NO_PAUSED : (visitors_held(L) >= LINK_VISIT_MAX || room_left(L) <= 0) ? LINK_NO_FULL : LINK_NO_OTHER;
        wr_t w; hdr(L, &w, FR_REFUSE, from); w32(&w, uid); w16(&w, trip); w8(&w, (uint32_t)why); tx(L, &w, from);
        return;
    }
    if (v) v->st = VS_FREE;                  /* its last trip's RETURN is answered by its being here again */
    memset(slot, 0, sizeof *slot);
    slot->st = VS_PENDING; slot->uid = uid; slot->home = from; slot->trip = trip; slot->edge = e; slot->t0 = L->now;
    slot->depth = depth; slot->heading = heading; slot->speed = speed; slot->pp = pp;
    send_fish(L, FR_ACCEPT, from, uid, trip);
}
static void rx_go(link_t *L, uint32_t from, uint32_t uid, uint16_t trip) {
    if (L->fry.on && L->fry.pp.uid == uid && L->fry.home == from) return;   /* a fry its home took, our OK to it still coming: not a DROP (two copies) */
    visit_t *v = visit_find(L, uid);
    if (!v || v->trip != trip || v->home != from) { if (!v || v->trip != trip) send_fish(L, FR_DROP, from, uid, trip); return; }
    if (v->st == VS_PENDING) {
        if (L->edge_peer[v->edge] != from) { v->st = VS_FREE; send_fish(L, FR_DROP, from, uid, trip); return; }
        v->st = VS_ACTIVE; v->lease_until = L->now + LINK_LEASE_LIFE_MS; v->reported = L->now;
        link_ev_t *ev = ev_push(L, LEV_VISITOR_IN);
        if (ev) { ev->uid = uid; ev->peer = from; ev->edge = v->edge; ev->depth = v->depth; ev->heading = v->heading; ev->speed = v->speed; ev->pp = v->pp; }
        send_visit(L, v);
    } else if (v->st == VS_ACTIVE) send_visit(L, v);
}
static void rx_visit(link_t *L, uint32_t from, rd_t *r) {
    uint32_t uid = r32(r); uint16_t trip = (uint16_t)r16(r);
    passport_t pp; bool ok = rpp(r, &pp);
    if (r->bad) return;
    away_t *a = away_find(L, uid);
    if (!a) { give_out_t *g = gout_find(L, uid); if (g && g->peer == from && g->st >= GO_TAKEN) return; }   /* (given there: theirs, not to be DROPped) */
    if (!a || a->st != AW_AWAY || a->trip != trip || a->peer != from) { send_fish(L, FR_DROP, from, uid, trip); return; }
    a->visit = L->now;
    if (ok && pp.uid == uid) { a->pp = pp; a->pp.home_tank = L->me; }
}
static void rx_lease(link_t *L, uint32_t from, uint32_t uid, uint16_t trip) {
    visit_t *v = visit_find(L, uid);
    if (v && v->st == VS_ACTIVE && v->trip == trip && v->home == from) v->lease_until = L->now + LINK_LEASE_LIFE_MS;
}
static void rx_return(link_t *L, uint32_t from, rd_t *r) {
    uint32_t uid = r32(r); uint16_t trip = (uint16_t)r16(r); uint32_t flags = r8(r), edge = r8(r);
    float depth, heading, speed; rmotion(r, &depth, &heading, &speed);
    passport_t pp; bool ok = rpp(r, &pp);
    if (r->bad) return;
    away_t *a = away_find(L, uid);
    if (a && a->st == AW_AWAY && a->trip == trip && a->peer == from) {
        if (ok && pp.uid == uid) { a->pp = pp; a->pp.home_tank = L->me; }
        link_edge_t e = edge < LINK_EDGES ? opp((link_edge_t)edge) : LINK_LEFT;
        bool door = !(flags & RF_POOF) && edge < LINK_EDGES && L->edge_peer[e] == from;
        link_ev_t *ev = ev_push(L, door ? LEV_FISH_BACK : LEV_FISH_RESTORE);
        if (ev) { ev->uid = uid; ev->peer = from; ev->edge = e; ev->depth = depth; ev->heading = heading; ev->speed = speed; ev->pp = a->pp; }
        a->st = AW_FREE;
    }
    send_fish(L, FR_HOME, from, uid, trip);  /* always: it stops the resends */
}
static void rx_drop(link_t *L, uint32_t from, uint32_t uid, uint16_t trip) {
    visit_t *v = visit_find(L, uid);         /* home says: that fish is not yours to keep */
    if (v && v->trip == trip && v->home == from && (v->st == VS_PENDING || v->st == VS_ACTIVE)) {
        if (v->st == VS_ACTIVE) { link_ev_t *ev = ev_push(L, LEV_VISITOR_POOF); if (ev) { ev->uid = uid; ev->peer = from; } }
        v->st = VS_FREE;
        return;
    }
    away_t *a = away_find(L, uid);           /* the host says: it never had it (a GO it could not place) */
    if (a && a->st == AW_AWAY && a->trip == trip && a->peer == from && !a->visit) {
        link_ev_t *ev = ev_push(L, LEV_FISH_RESTORE); if (ev) { ev->uid = uid; ev->peer = from; ev->pp = a->pp; }
        a->st = AW_FREE;
    }
}
static void rx_recall(link_t *L, uint32_t from, uint32_t uid, uint16_t trip) {
    visit_t *v = visit_find(L, uid);
    if (!v || v->trip != trip || v->home != from) return;
    if (v->st == VS_ACTIVE) visitor_poof(L, v);
    else if (v->st == VS_RETURNING) send_return(L, v);
    else if (v->st == VS_PENDING) { v->st = VS_FREE; send_fish(L, FR_DROP, from, uid, trip); }
}
/* home: does the tank next door's visitor - one of ours - have room at home for a fry?
   A yes HOLDS the place (host.fry_hold) while the host keeps asking: nothing else here may take it before the fry
   comes (2026-11-03: a yes held nothing - by the birth, 5-10 min on, the place was gone, the FRY was refused, and the
   host kept the fry as a sixth fish). off: the host's pair is off - let go, no answer */
static void rx_fry_ask(link_t *L, uint32_t from, uint32_t uid, uint16_t trip, int flag) {
    away_t *a = away_find(L, uid);
    bool ours = a && a->st == AW_AWAY && a->peer == from && a->trip == trip;
    if (flag == FRY_ASK_OFF) { if (L->host.fry_hold) L->host.fry_hold(L->host.ctx, from, uid, false); return; }
    int room = !ours ? 0 : flag == FRY_ASK_PEEK ? (L->host.fry_room ? L->host.fry_room(L->host.ctx) : 0)   /* (a peek: nothing held) */
             : L->host.fry_hold ? L->host.fry_hold(L->host.ctx, from, uid, true)
             : L->host.fry_room ? L->host.fry_room(L->host.ctx) : 0;
    wr_t w; hdr(L, &w, FR_FRY_ROOM, from); w32(&w, uid); w16(&w, trip); w8(&w, room > 0); tx(L, &w, from);
}
/* home: a fry of ours, born next door. It is ours from here on - away at the
   tank it was born in, on a trip of its own, and that tank holds it as a
   visitor going home. Never two: the host draws it only on our OK; never
   zero: no VISIT from the host (our OK lost) and it comes home from bubbles */
static void rx_fry(link_t *L, uint32_t from, rd_t *r) {
    uint32_t parent = r32(r); uint32_t edge = r8(r);
    passport_t pp; bool ok = rpp(r, &pp);
    if (r->bad || !ok || !pp.uid) return;
    link_kin_t kin; memset(&kin, 0, sizeof kin);
    if (r->n - r->p >= 4 + FISH_NAME_MAX + 5) {      /* its other parent, when the sender sends it */
        kin.uid = r32(r);
        for (int i = 0; i < FISH_NAME_MAX; i++) { char c = (char)r8(r); kin.name[i] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : (c ? '?' : 0); }
        kin.color = r8(r) << 16; kin.color |= r8(r) << 8; kin.color |= r8(r);
        kin.bold = (uint8_t)r8(r); kin.sociable = (uint8_t)r8(r);
        if (r->bad) memset(&kin, 0, sizeof kin);
    }
    (void)edge;
    away_t *a = away_find(L, pp.uid);
    if (a && a->peer == from) { send_fish(L, FR_FRY_OK, from, pp.uid, a->trip); return; }   /* (our OK was lost) */
    away_t *pa = away_find(L, parent);
    link_edge_t e = L->edge_peer[LINK_LEFT] == from ? LINK_LEFT : LINK_RIGHT;
    bool yes = !a && pa && pa->st == AW_AWAY && pa->peer == from && L->edge_peer[e] == from && !tank_paused(L, from)
            && pp.home_tank == L->me
            && (L->host.fry_hold ? L->host.fry_hold(L->host.ctx, from, parent, true) > 0   /* (its held place, or one free now) */
                                 : L->host.fry_room && L->host.fry_room(L->host.ctx) > 0);
    if (yes) { a = NULL; for (int i = 0; i < LINK_AWAY_MAX && !a; i++) if (L->away[i].st == AW_FREE) a = &L->away[i]; }
    if (!yes || !a) {
        if (L->host.fry_hold) L->host.fry_hold(L->host.ctx, from, parent, false);   /* (no: nothing to hold it for) */
        send_fish(L, FR_FRY_NO, from, pp.uid, 0); return;
    }
    memset(a, 0, sizeof *a);
    a->st = AW_AWAY; a->uid = pp.uid; a->peer = from; a->trip = ++L->trip; a->edge = e;
    a->t0 = a->sent = L->now; a->pp = pp; a->pp.home_tank = L->me;
    link_ev_t *ev = ev_push(L, LEV_FRY_IN); if (ev) { ev->uid = pp.uid; ev->peer = from; ev->edge = e; ev->pp = a->pp; ev->kin = kin; }
    send_fish(L, FR_FRY_OK, from, pp.uid, a->trip);
}
/* host: the fry's home took it - it is a visitor here now, going home */
static void rx_fry_ok(link_t *L, uint32_t from, uint32_t uid, uint16_t trip) {
    if (!L->fry.on || L->fry.pp.uid != uid || L->fry.home != from) return;
    L->fry.on = false;
    visit_t *v = NULL;
    for (int i = 0; i <= LINK_VISIT_MAX && !v; i++) if (L->visit[i].st == VS_FREE) v = &L->visit[i];
    if (!v) { send_fish(L, FR_DROP, from, uid, trip); return; }   /* (no place after all: home restores it from bubbles) */
    memset(v, 0, sizeof *v);
    v->st = VS_ACTIVE; v->uid = uid; v->home = from; v->trip = trip; v->edge = L->fry.edge; v->t0 = L->now;
    v->lease_until = L->now + LINK_LEASE_LIFE_MS; v->reported = L->now; v->pp = L->fry.pp;
    link_ev_t *ev = ev_push(L, LEV_FRY_OUT); if (ev) { ev->uid = uid; ev->peer = from; ev->edge = v->edge; ev->pp = v->pp; }
    send_visit(L, v);
}
/* ---- GIVE (phase 6; link.h has the three rules) ---- */
static give_out_t *gout_find(link_t *L, uint32_t uid) {
    for (int i = 0; i < LINK_GIVE_MAX; i++) if (L->gout[i].st != GO_FREE && L->gout[i].uid == uid) return &L->gout[i];
    return NULL;
}
static give_in_t *gin_find(link_t *L, uint32_t uid) {
    for (int i = 0; i < LINK_GIVE_MAX; i++) if (L->gin[i].st != GI_FREE && L->gin[i].uid == uid) return &L->gin[i];
    return NULL;
}
static int give_state(link_t *L, uint32_t uid, uint32_t nonce, passport_t *pp) {
    return L->host.give_state ? L->host.give_state(L->host.ctx, uid, nonce, pp) : LINK_GS_NONE;
}
static void send_give(link_t *L, int type, uint32_t to, uint32_t uid, uint32_t nonce, int reason, const passport_t *pp) {
    wr_t w; hdr(L, &w, type, to); w32(&w, uid); w32(&w, nonce);
    if (reason >= 0) w8(&w, (uint32_t)reason);
    if (pp) wpp(&w, pp);
    tx(L, &w, to);
}
static void give_ev(link_t *L, link_ev_type_t type, uint32_t uid, uint32_t peer, uint32_t nonce, const passport_t *pp, int reason) {
    link_ev_t *ev = ev_push(L, type);
    if (!ev) return;
    ev->uid = uid; ev->peer = peer; ev->nonce = nonce; ev->reason = (uint8_t)(reason < 0 ? 0 : reason);
    if (pp) ev->pp = *pp;
}
uint32_t link_give_nonce(const link_t *L, uint32_t uid) {
    for (int i = 0; i < LINK_GIVE_MAX; i++) if (L->gout[i].st != GO_FREE && L->gout[i].uid == uid) return L->gout[i].nonce;
    return 0;
}
bool link_give_busy(const link_t *L, uint32_t uid) {
    for (int i = 0; i < LINK_GIVE_MAX; i++) if (L->gout[i].st != GO_FREE && L->gout[i].uid == uid) return true;
    return false;
}
int link_away_edge(const link_t *L, uint32_t uid) {
    for (int i = 0; i < LINK_AWAY_MAX; i++) {
        const away_t *a = &L->away[i];
        if (a->st == AW_AWAY && a->uid == uid) for (int e = 0; e < LINK_EDGES; e++) if (link_door_open(L, (link_edge_t)e) && L->edge_peer[e] == a->peer) return e;
    }
    return -1;
}
bool link_give(link_t *L, link_edge_t e, const passport_t *pp) {
    away_t *aw = pp ? away_find(L, pp->uid) : NULL;     /* one of ours next door: given where it is, to the tank it is visiting
                                                           (2026-11-01, Strato: a fish that swam off on its own must not stop the gift) */
    if (aw && !(aw->st == AW_AWAY && aw->peer == L->edge_peer[e])) return false;
    if (!pp || !pp->uid || !link_door_open(L, e) || gout_find(L, pp->uid) || visit_find(L, pp->uid)) return false;
    give_out_t *g = NULL;
    for (int i = 0; i < LINK_GIVE_MAX && !g; i++) if (L->gout[i].st == GO_FREE) g = &L->gout[i];
    if (!g) return false;
    memset(g, 0, sizeof *g);
    g->st = GO_ASKING; g->uid = pp->uid; g->peer = L->edge_peer[e]; g->t0 = g->sent = L->now; g->pp = aw ? aw->pp : *pp;   /* (away: the host's latest of it) */
    g->nonce = ((L->me ^ 0x9e3779b9u) * 2654435761u) ^ ((uint32_t)++L->trip << 16) ^ L->now;
    if (!g->nonce) g->nonce = 1;
    send_give(L, FR_GIVE, g->peer, g->uid, g->nonce, -1, &g->pp);
    return true;
}
void link_give_cancel(link_t *L, uint32_t uid) {
    give_out_t *g = gout_find(L, uid);
    if (!g || g->st != GO_ASKING) return;    /* (taken: too late - the gift goes on) */
    send_give(L, FR_GIVE_CANCEL, g->peer, g->uid, g->nonce, -1, NULL);
    give_ev(L, LEV_GIVE_NO, g->uid, g->peer, g->nonce, NULL, LINK_GIVE_NO_CANCEL);
    g->st = GO_FREE;
}
bool link_give_keep(link_t *L, uint32_t uid, uint32_t nonce) {
    give_out_t *g = gout_find(L, uid);
    if (!g || g->st != GO_TAKEN || g->nonce != nonce) return false;   /* (GIVEN sent: too late - it is theirs) */
    if (linked_to(L, g->peer)) send_give(L, FR_GIVE_CANCEL, g->peer, g->uid, g->nonce, -1, NULL);
    g->st = GO_FREE;                         /* (a TAKE after this, now or at a relink, is answered CANCEL: no note) */
    return true;
}
bool link_peer_heard(const link_t *L, uint32_t tank, uint32_t within_ms) {
    for (int i = 0; i < LINK_PEERS_MAX; i++)
        if (L->peer[i].used && L->peer[i].tank == tank) return !after(L->now, L->peer[i].heard + within_ms);
    return false;
}
void link_give_done(link_t *L, uint32_t uid, uint32_t nonce, const passport_t *pp) {
    give_out_t *g = gout_find(L, uid);
    if (!g || g->st != GO_TAKEN || g->nonce != nonce) return;
    away_t *a = away_find(L, uid);                       /* given where it is: no lease from now on - it is theirs (its VISITs are not DROPped) */
    if (a && a->peer == g->peer) a->st = AW_FREE;
    if (pp && pp->uid == uid) g->pp = *pp;               /* the fish as it LEFT (its GIVEN note's), not as it was when the keeper asked: GIVEN
                                                            carries it, and a GIVEN_LOST brings this one back (0.4.1 audit round 1,
                                                            R-seam-link-progression F3 - a stage reached in the window was paid twice) */
    g->st = GO_GIVEN; g->sent = L->now;
    if (linked_to(L, g->peer)) send_give(L, FR_GIVEN, g->peer, g->uid, g->nonce, -1, &g->pp);
}
void link_give_resume_out(link_t *L, uint32_t peer, uint32_t uid, uint32_t nonce, const passport_t *pp) {
    give_out_t *g = gout_find(L, uid);
    if (g) return;
    for (int i = 0; i < LINK_GIVE_MAX && !g; i++) if (L->gout[i].st == GO_FREE) g = &L->gout[i];
    if (!g) return;                          /* (the next LINKED tries again) */
    memset(g, 0, sizeof *g);
    g->st = GO_GIVEN; g->uid = uid; g->nonce = nonce; g->peer = peer; g->t0 = g->sent = L->now; if (pp) g->pp = *pp;
    if (linked_to(L, peer)) send_give(L, FR_GIVEN, peer, uid, nonce, -1, pp ? &g->pp : NULL);
}
bool link_give_refuse(link_t *L, uint32_t uid, uint32_t nonce, int reason) {
    give_in_t *g = gin_find(L, uid);
    if (!g || g->st != GI_ASKING || g->nonce != nonce) return false;
    send_give(L, FR_GIVE_NO, g->from, g->uid, g->nonce, reason, NULL); g->st = GI_FREE;
    return true;
}
bool link_give_answer(link_t *L, uint32_t uid, uint32_t nonce, bool yes) {
    give_in_t *g = gin_find(L, uid);
    if (!g || g->st != GI_ASKING || g->nonce != nonce) return false;
    if (yes) { g->st = GI_TAKE; g->sent = L->now; send_give(L, FR_GIVE_TAKE, g->from, g->uid, g->nonce, -1, NULL); return true; }
    return link_give_refuse(L, uid, nonce, LINK_GIVE_NO_REFUSED);   /* (the keeper's own NO THANKS) */
}
void link_give_alive(link_t *L, uint32_t uid, uint32_t nonce) {
    give_in_t *g = gin_find(L, uid);
    if (!g || g->st != GI_ALIVE || g->nonce != nonce) return;
    send_give(L, FR_GIVEN_OK, g->from, g->uid, g->nonce, -1, NULL);
    g->st = GI_FREE;
}
void link_give_resume_in(link_t *L, uint32_t from, uint32_t uid, uint32_t nonce, const passport_t *pp) {
    give_in_t *g = gin_find(L, uid);
    if (g) return;
    for (int i = 0; i < LINK_GIVE_MAX && !g; i++) if (L->gin[i].st == GI_FREE) g = &L->gin[i];
    if (!g) return;
    memset(g, 0, sizeof *g);
    g->st = GI_TAKE; g->uid = uid; g->nonce = nonce; g->from = from; g->t0 = g->sent = L->now; if (pp) g->pp = *pp;
    if (linked_to(L, from)) send_give(L, FR_GIVE_TAKE, from, uid, nonce, -1, NULL);
}
/* the receiving side */
static void rx_give(link_t *L, uint32_t from, uint32_t uid, uint32_t nonce, rd_t *r) {
    passport_t pp; bool ok = rpp(r, &pp);
    if (r->bad) return;
    give_in_t *g = gin_find(L, uid);
    if (g && g->nonce == nonce && g->from == from) {     /* (a resend: it is still being asked, or our TAKE was lost) */
        if (g->st == GI_TAKE) send_give(L, FR_GIVE_TAKE, from, uid, nonce, -1, NULL);
        return;
    }
    passport_t held;
    int gs = give_state(L, uid, nonce, &held);
    if (gs == LINK_GS_ARRIVING) { link_give_resume_in(L, from, uid, nonce, &held); return; }   /* (we said yes before a boot) */
    int no = -1;
    const visit_t *vh = visit_find(L, uid);             /* visiting us: given where it is - only from its own home, while it is here */
    if (!ok || pp.uid != uid || g || gs != LINK_GS_NONE || (vh && !(vh->home == from && vh->st == VS_ACTIVE))) no = LINK_GIVE_NO_OTHER;
    else if (!linked_to(L, from) || tank_paused(L, from)) no = LINK_GIVE_NO_OTHER;
    else if (!L->host.fry_room || L->host.fry_room(L->host.ctx) <= 0) no = LINK_GIVE_NO_FULL;
    if (no < 0) {
        for (int i = 0; i < LINK_GIVE_MAX && !g; i++) if (L->gin[i].st == GI_FREE) g = &L->gin[i];
        if (!g) no = LINK_GIVE_NO_OTHER;
    }
    if (no >= 0) { send_give(L, FR_GIVE_NO, from, uid, nonce, no, NULL); return; }
    memset(g, 0, sizeof *g);
    g->st = GI_ASKING; g->uid = uid; g->nonce = nonce; g->from = from; g->t0 = g->sent = L->now; g->pp = pp;
    give_ev(L, LEV_GIVE_ASK, uid, from, nonce, &pp, -1);
}
static void visit_settle(link_t *L, uint32_t uid, uint32_t from) {   /* a gift of a fish visiting here comes alive: ours, no visit */
    visit_t *v = visit_find(L, uid);
    if (v && v->home == from && v->st == VS_ACTIVE) v->st = VS_FREE;
}
static void rx_given(link_t *L, uint32_t from, uint32_t uid, uint32_t nonce, rd_t *r) {
    passport_t left; bool has = false;                   /* (0.4.1: the fish as it left the giver - a 0.4.0 tank sends none: the ask's) */
    if (r->p < r->n) { has = rpp(r, &left) && !r->bad && left.uid == uid; }
    give_in_t *g = gin_find(L, uid);
    if (g && g->nonce == nonce && g->from == from) {
        if (g->st == GI_TAKE) { if (has) g->pp = left; g->st = GI_ALIVE; visit_settle(L, uid, from); give_ev(L, LEV_GIVE_ALIVE, uid, from, nonce, &g->pp, -1); }
        return;                              /* (GI_ALIVE: the tank is saving - its GIVEN_OK follows) */
    }
    passport_t held;
    int gs = give_state(L, uid, nonce, &held);
    if (gs == LINK_GS_ALIVE) send_give(L, FR_GIVEN_OK, from, uid, nonce, -1, NULL);   /* (our OK was lost) */
    else if (gs == LINK_GS_ARRIVING) {       /* (a boot forgot it, the note did not) */
        link_give_resume_in(L, from, uid, nonce, &held);
        g = gin_find(L, uid);
        if (g && g->nonce == nonce) { if (has) g->pp = left; g->st = GI_ALIVE; visit_settle(L, uid, from); give_ev(L, LEV_GIVE_ALIVE, uid, from, nonce, &g->pp, -1); }
    } else send_give(L, FR_GIVEN_LOST, from, uid, nonce, -1, NULL);   /* we never held it: the giver's note brings it back */
}
/* the receiver lets go of a gift it said yes to, the giver gone for good (tank_link: an ARRIVING note past its time):
   the engine's waiting copy goes too, so a GIVEN that comes after all is answered GIVEN_LOST - the giver's own note
   brings the fish back there, never a second one here */
void link_give_forget(link_t *L, uint32_t uid, uint32_t nonce) {
    give_in_t *g = gin_find(L, uid);
    if (g && g->nonce == nonce && g->st == GI_TAKE) g->st = GI_FREE;
}
static void rx_give_cancel(link_t *L, uint32_t from, uint32_t uid, uint32_t nonce) {
    give_in_t *g = gin_find(L, uid);
    if (g && g->nonce == nonce && g->from == from) {
        if (g->st == GI_ALIVE) return;       /* (cannot happen: the giver cancels only before GIVEN) */
        give_ev(L, LEV_GIVE_CANCEL, uid, from, nonce, NULL, -1);
        g->st = GI_FREE;
        return;
    }
    passport_t held;
    if (!g && give_state(L, uid, nonce, &held) == LINK_GS_ARRIVING) give_ev(L, LEV_GIVE_CANCEL, uid, from, nonce, NULL, -1);
}
/* the giving side */
static void rx_give_take(link_t *L, uint32_t from, uint32_t uid, uint32_t nonce) {
    give_out_t *g = gout_find(L, uid);
    if (g && g->nonce == nonce && g->peer == from) {
        if (g->st == GO_ASKING) { g->st = GO_TAKEN; give_ev(L, LEV_GIVE_TAKEN, uid, from, nonce, &g->pp, -1); }
        else if (g->st == GO_GIVEN) send_give(L, FR_GIVEN, from, uid, nonce, -1, g->pp.uid ? &g->pp : NULL);
        return;
    }
    passport_t held;
    if (!g && give_state(L, uid, nonce, &held) == LINK_GS_GIVEN) { link_give_resume_out(L, from, uid, nonce, &held); return; }
    send_give(L, FR_GIVE_CANCEL, from, uid, nonce, -1, NULL);   /* not giving it (any more): it is ours */
}
static void rx_give_reply(link_t *L, int type, uint32_t from, uint32_t uid, uint32_t nonce, int reason) {
    give_out_t *g = gout_find(L, uid);
    if (!g || g->nonce != nonce || g->peer != from) return;
    if (type == FR_GIVE_NO && g->st == GO_ASKING) {
        give_ev(L, LEV_GIVE_NO, uid, from, nonce, NULL, reason <= LINK_GIVE_NO_OTHER ? reason : LINK_GIVE_NO_OTHER);
        g->st = GO_FREE;
    } else if (type == FR_GIVEN_OK && g->st == GO_GIVEN) {
        give_ev(L, LEV_GIVE_CLOSED, uid, from, nonce, NULL, -1);
        g->st = GO_FREE;
    } else if (type == FR_GIVEN_LOST && g->st == GO_GIVEN) {
        give_ev(L, LEV_GIVE_RESTORE, uid, from, nonce, &g->pp, -1);
        g->st = GO_FREE;
    }
}
static void give_tick(link_t *L, uint32_t now) {
    for (int i = 0; i < LINK_GIVE_MAX; i++) {
        give_out_t *g = &L->gout[i];
        if (g->st == GO_ASKING) {
            if (!linked_to(L, g->peer) || after(now, g->t0 + LINK_GIVE_ASK_MS + 2000)) {   /* (a little past their keeper's time) */
                if (linked_to(L, g->peer)) send_give(L, FR_GIVE_CANCEL, g->peer, g->uid, g->nonce, -1, NULL);
                give_ev(L, LEV_GIVE_NO, g->uid, g->peer, g->nonce, NULL, LINK_GIVE_NO_ANSWER);
                g->st = GO_FREE;
            } else if (after(now, g->sent + LINK_GIVE_RESEND_MS)) { send_give(L, FR_GIVE, g->peer, g->uid, g->nonce, -1, &g->pp); g->sent = now; }
        } else if (g->st == GO_GIVEN && linked_to(L, g->peer) && after(now, g->sent + LINK_GIVE_RESEND_MS)) {
            send_give(L, FR_GIVEN, g->peer, g->uid, g->nonce, -1, g->pp.uid ? &g->pp : NULL); g->sent = now;
        } else if (g->st == GO_GIVEN && !linked_to(L, g->peer)) g->st = GO_FREE;   /* (0.4.1) the tank is gone: its slot goes. The GIVEN
                                                           note saved before GIVEN holds the gift, and brings it back here (tank_link's
                                                           resume) - two slots held for a tank that walked off left none for a gift */
    }
    for (int i = 0; i < LINK_GIVE_MAX; i++) {
        give_in_t *g = &L->gin[i];
        if (g->st == GI_ASKING && (!linked_to(L, g->from) || after(now, g->t0 + LINK_GIVE_ASK_MS))) {
            if (linked_to(L, g->from)) send_give(L, FR_GIVE_NO, g->from, g->uid, g->nonce, LINK_GIVE_NO_ANSWER, NULL);
            give_ev(L, LEV_GIVE_CANCEL, g->uid, g->from, g->nonce, NULL, -1);   /* (the question closes) */
            g->st = GI_FREE;
        } else if (g->st == GI_TAKE && linked_to(L, g->from) && after(now, g->sent + LINK_GIVE_RESEND_MS)) {
            send_give(L, FR_GIVE_TAKE, g->from, g->uid, g->nonce, -1, NULL); g->sent = now;
        } else if ((g->st == GI_TAKE || g->st == GI_ALIVE) && !linked_to(L, g->from)) g->st = GI_FREE;   /* (0.4.1: the same - its ARRIVING
                                                           note holds it; GI_ALIVE's event carries the fish, and a lost GIVEN_OK is
                                                           answered from RECEIVED at the next meeting) */
    }
}

/* a fish of ours out, or a visitor here (not one already on its way home) */
static bool traffic(const link_t *L) {
    for (int i = 0; i < LINK_AWAY_MAX; i++) if (L->away[i].st != AW_FREE) return true;
    for (int i = 0; i <= LINK_VISIT_MAX; i++) if (L->visit[i].st == VS_PENDING || L->visit[i].st == VS_ACTIVE) return true;
    return false;
}
static void homeward(link_t *L, uint32_t home) {
    for (int i = 0; i <= LINK_VISIT_MAX; i++)
        if (L->visit[i].st == VS_ACTIVE && L->visit[i].home == home) {
            link_ev_t *ev = ev_push(L, LEV_VISITOR_HOMEWARD); if (ev) { ev->uid = L->visit[i].uid; ev->peer = home; ev->edge = L->visit[i].edge; }
        }
}

void link_rx(link_t *L, const uint8_t *buf, int len, int rssi, uint32_t now_ms) {
    L->now = now_ms;
    if (!buf || len < HDR || len > LINK_FRAME_MAX) return;
    rd_t r = { buf, len, 0, false };
    uint32_t magic = r16(&r), proto = r8(&r), type = r8(&r), from = r32(&r), to = r32(&r);
    if (magic != LINK_MAGIC || proto != LINK_PROTO || type == 0 || type >= FR_TYPES) return;
    if (!from || from == L->me || (to && to != L->me)) return;
    peer_t *p = peer_get(L, from);
    if (p) { p->heard = now_ms; p->rssi = rssi; }
    switch (type) {
    case FR_BEACON: if (p) rx_beacon(L, p, &r); break;
    case FR_JOIN: case FR_LINKED: case FR_UNLINK: case FR_GOHOME: {
        uint32_t e = r8(&r), why = r8(&r);
        if (r.bad || e >= LINK_EDGES) return;
        if (type == FR_JOIN) rx_join(L, from, (link_edge_t)e);
        else if (type == FR_LINKED) rx_linked(L, from, (link_edge_t)e);
        else if (type == FR_UNLINK) { if (L->edge_peer[opp((link_edge_t)e)] == from) unlink_edge(L, opp((link_edge_t)e), why <= LINK_WHY_MISMATCH ? (link_why_t)why : LINK_WHY_LOST, false); }
        else if (L->edge_peer[opp((link_edge_t)e)] == from) homeward(L, from);
        break;
    }
    case FR_OFFER: rx_offer(L, from, &r); break;
    case FR_VISIT: rx_visit(L, from, &r); break;
    case FR_RETURN: rx_return(L, from, &r); break;
    case FR_FRY: rx_fry(L, from, &r); break;
    case FR_GIVE: case FR_GIVE_TAKE: case FR_GIVE_NO: case FR_GIVE_CANCEL: case FR_GIVEN: case FR_GIVEN_OK: case FR_GIVEN_LOST: {
        uint32_t uid = r32(&r), nonce = r32(&r);
        if (r.bad || !uid || !nonce) return;
        if (type == FR_GIVE) rx_give(L, from, uid, nonce, &r);
        else if (type == FR_GIVE_TAKE) rx_give_take(L, from, uid, nonce);
        else if (type == FR_GIVE_CANCEL) rx_give_cancel(L, from, uid, nonce);
        else if (type == FR_GIVEN) rx_given(L, from, uid, nonce, &r);
        else { int why = type == FR_GIVE_NO ? (int)r8(&r) : 0; if (!r.bad) rx_give_reply(L, (int)type, from, uid, nonce, why); }
        break;
    }
    default: {
        uint32_t uid = r32(&r); uint16_t trip = (uint16_t)r16(&r);
        if (r.bad || !uid) return;
        away_t *a = away_find(L, uid);
        if (type == FR_ACCEPT || type == FR_REFUSE) {
            if (!a || a->st != AW_OFFERING || a->trip != trip || a->peer != from) return;
            uint32_t why = type == FR_REFUSE && r.p < r.n ? r8(&r) : LINK_NO_OTHER;   /* (a reason byte since 2026-10-05) */
            link_ev_t *ev = ev_push(L, type == FR_ACCEPT ? LEV_CROSS_OK : LEV_CROSS_NO); if (ev) { ev->uid = uid; ev->peer = from; ev->edge = a->edge; ev->reason = (uint8_t)why; }
            if (type == FR_ACCEPT) { a->st = AW_ACCEPTED; a->t0 = L->now; } else a->st = AW_FREE;
        } else if (type == FR_GO) rx_go(L, from, uid, trip);
        else if (type == FR_LEASE) rx_lease(L, from, uid, trip);
        else if (type == FR_HOME) { visit_t *v = visit_find(L, uid); if (v && v->st == VS_RETURNING && v->trip == trip && v->home == from) v->st = VS_FREE; }
        else if (type == FR_RECALL) rx_recall(L, from, uid, trip);
        else if (type == FR_DROP) rx_drop(L, from, uid, trip);
        else if (type == FR_FRY_ASK) { int fl = r.p < r.n ? (int)r8(&r) : FRY_ASK_HOLD; rx_fry_ask(L, from, uid, trip, fl == FRY_ASK_OFF || fl == FRY_ASK_PEEK ? fl : FRY_ASK_HOLD); }   /* (the flag since
                                                         2026-11-03; PEEK since 0.4.1 round 2; an unknown value: an ask, as before) */
        else if (type == FR_FRY_ROOM) {
            uint32_t yes = r8(&r);
            visit_t *v = visit_find(L, uid);
            if (!r.bad && v && v->home == from && v->trip == trip) { link_ev_t *ev = ev_push(L, LEV_FRY_ROOM); if (ev) { ev->uid = uid; ev->peer = from; ev->reason = yes ? 1 : 0; } }
        }
        else if (type == FR_FRY_OK) rx_fry_ok(L, from, uid, trip);
        else if (type == FR_FRY_NO) {
            if (L->fry.on && L->fry.pp.uid == uid && L->fry.home == from) {
                L->fry.on = false;
                link_ev_t *ev = ev_push(L, LEV_FRY_KEEP); if (ev) { ev->uid = uid; ev->peer = from; ev->pp = L->fry.pp; }
            }
        }
    }
    }
}

/* ---- time ---- */
void link_tick(link_t *L, uint32_t now) {
    L->now = now;
    bool any_link = L->edge_peer[0] || L->edge_peer[1];
    if (L->mode && after(now, L->mode_active + LINK_MODE_IDLE_MS)) link_mode(L, false);

    /* the edges: lost, mismatched */
    for (int e = 0; e < LINK_EDGES; e++) {
        uint32_t pt = L->edge_peer[e];
        if (!pt) continue;
        peer_t *p = peer_find(L, pt);
        if (!p || after(now, p->heard + LINK_PEER_GONE_MS)) { unlink_edge(L, (link_edge_t)e, LINK_WHY_LOST, false); continue; }
        if (fresh(L, p) && p->edge_peer[opp((link_edge_t)e)] != L->me) {
            if (!L->mismatch_since[e]) L->mismatch_since[e] = now;
            else if (after(now, L->mismatch_since[e] + LINK_MISMATCH_MS)) unlink_edge(L, (link_edge_t)e, LINK_WHY_MISMATCH, true);
        } else L->mismatch_since[e] = 0;
    }
    /* joining: the arrows, and a JOIN to the one tank asking at the facing edge */
    for (int e = 0; e < LINK_EDGES; e++) {
        uint32_t one = 0; int n = L->edge_peer[e] || !L->mode ? 0 : candidates(L, (link_edge_t)e, &one);
        uint32_t who = n == 1 ? one : 0; uint8_t nn = (uint8_t)(n > 2 ? 2 : n);
        if (who != L->asking[e] || nn != L->asking_n[e]) {
            L->asking[e] = who; L->asking_n[e] = nn;
            link_ev_t *ev = ev_push(L, LEV_ASKING); if (ev) { ev->edge = (link_edge_t)e; ev->peer = who; ev->reason = nn; }
        }
        if (n == 1 && armed(L, (link_edge_t)e) && after(now, L->join_sent[e] + LINK_JOIN_RESEND_MS)) {
            send_edge(L, FR_JOIN, one, (link_edge_t)e, 0); L->join_sent[e] = now;
        }
    }
    /* drifting (forgiven: a nudge is nothing; a drift pauses the doors with a
       notice; still again = nothing happened; past the grace the visitors head
       home, and after that the doors close) */
    any_link = L->edge_peer[0] || L->edge_peer[1];
    bool settling = L->settle_until && !after(now, L->settle_until);
    if (L->moving && !settling) { L->still_since = 0; if (!L->move_since) L->move_since = now ? now : 1; }
    else { L->move_since = 0; if (!L->still_since) L->still_since = now ? now : 1; }
    if (!L->drifting && any_link && L->move_since && after(now, L->move_since + LINK_NUDGE_MS)) {
        L->drifting = true; L->drift_since = now; L->homeward = false;
    }
    if (L->drifting && !L->homeward && L->still_since && after(now, L->still_since + LINK_STILL_MS)) L->drifting = false;
    if (L->drifting && !any_link) L->drifting = false;
    if (L->drifting && !L->homeward && after(now, L->drift_since + LINK_DRIFT_GRACE_MS)) {
        L->homeward = true; L->homeward_at = now;
        for (int e = 0; e < LINK_EDGES; e++) if (L->edge_peer[e]) { send_edge(L, FR_GOHOME, L->edge_peer[e], (link_edge_t)e, 0); homeward(L, L->edge_peer[e]); }
    }
    /* the doors close once the visitors are home - at once when nobody was out
       (2026-10-05: with no fish visiting, the notice sat at an empty bar for
       the whole homeward time and looked hung) - or at LINK_HOMEWARD_MS */
    if (L->drifting && L->homeward && (!traffic(L) || after(now, L->homeward_at + LINK_HOMEWARD_MS))) {
        for (int e = 0; e < LINK_EDGES; e++) unlink_edge(L, (link_edge_t)e, LINK_WHY_DRIFT, true);
        L->drifting = false; L->homeward = false;
    }
    bool notice = L->drifting;
    for (int e = 0; e < LINK_EDGES && !notice; e++) {
        peer_t *p = L->edge_peer[e] ? peer_find(L, L->edge_peer[e]) : NULL;
        if (fresh(L, p) && (p->flags & BF_DRIFT)) notice = true;
    }
    if (notice != L->notice) {
        L->notice = notice;
        /* STEADY only when a door is open again: a notice that ends because the doors
           closed (DISCONNECT, the grace run out) is no "doors open" (2026-10-05 - a
           sound cue would have chimed the wrong news) */
        link_ev_t *ev = notice || L->edge_peer[0] || L->edge_peer[1] ? ev_push(L, notice ? LEV_DRIFT : LEV_STEADY) : NULL;
        if (ev && notice) ev->ms_left = L->drifting ? LINK_DRIFT_GRACE_MS - (now - L->drift_since) : LINK_DRIFT_GRACE_MS;
    }
    /* beacons */
    if ((L->mode || L->edge_peer[0] || L->edge_peer[1]) && after(now, L->beacon_at)) {
        send_beacon(L); L->beacon_at = now + (L->mode ? LINK_BEACON_MODE_MS : LINK_BEACON_LINKED_MS);
    }
    /* our fish, heading out and away */
    for (int i = 0; i < LINK_AWAY_MAX; i++) {
        away_t *a = &L->away[i];
        if (a->st == AW_OFFERING) {
            if (after(now, a->t0 + LINK_OFFER_MS)) { link_ev_t *ev = ev_push(L, LEV_CROSS_NO); if (ev) ev->uid = a->uid; a->st = AW_FREE; }
            else if (after(now, a->sent + LINK_RESEND_MS)) { send_offer(L, a); a->sent = now; }
        } else if (a->st == AW_ACCEPTED) {
            if (after(now, a->t0 + LINK_CROSS_MS)) { link_ev_t *ev = ev_push(L, LEV_CROSS_NO); if (ev) ev->uid = a->uid; a->st = AW_FREE; }
        } else if (a->st == AW_AWAY) {
            /* GO until the host answers: VISIT (it has the fish) or DROP (it
               never did - its held place ran out). Quickly at first, then
               slower past LINK_PENDING_MS, so a burst of lost GOs ends in a
               DROP and the fish is home at once, not after LINK_RESTORE_MS */
            if (!a->visit && after(now, a->sent + (after(now, a->t0 + LINK_PENDING_MS) ? LINK_GO_SLOW_MS : LINK_RESEND_MS))) {
                send_fish(L, FR_GO, a->peer, a->uid, a->trip); a->sent = now;
            }
            if (a->visit && !after(now, a->visit + LINK_LEASE_HEARD_MS) && (!a->lease_sent || after(now, a->lease_sent + LINK_LEASE_MS))) {
                send_fish(L, FR_LEASE, a->peer, a->uid, a->trip); a->lease_sent = now;
            }
            if (after(now, a->visit ? a->visit + LINK_RESTORE_MS : a->t0 + LINK_RESTORE_NOVISIT_MS)) {
                link_ev_t *ev = ev_push(L, LEV_FISH_RESTORE); if (ev) { ev->uid = a->uid; ev->peer = a->peer; ev->pp = a->pp; }
                a->st = AW_FREE;
            }
        }
    }
    /* a fry being handed over: FRY until its home answers OK or NO; no answer
       in LINK_CROSS_MS = no fry here (if its home took it after all, the fry
       comes home from bubbles there - never two) */
    if (L->fry.on) {
        if (after(now, L->fry.t0 + LINK_CROSS_MS) || !linked_to(L, L->fry.home)) {
            L->fry.on = false;
            link_ev_t *ev = ev_push(L, LEV_FRY_LOST); if (ev) { ev->uid = L->fry.pp.uid; ev->peer = L->fry.home; }
        } else if (after(now, L->fry.sent + LINK_RESEND_MS)) { send_fry(L); L->fry.sent = now; }
    }
    give_tick(L, now);
    /* visitors */
    for (int i = 0; i <= LINK_VISIT_MAX; i++) {
        visit_t *v = &L->visit[i];
        if (v->st == VS_PENDING) { if (after(now, v->t0 + LINK_PENDING_MS)) v->st = VS_FREE; }
        else if (v->st == VS_ACTIVE) {
            if (after(now, v->lease_until)) { visitor_poof(L, v); continue; }
            if (after(now, v->reported + LINK_VISIT_MS)) { visitor_fresh_pp(L, v); send_visit(L, v); v->reported = now; }
        } else if (v->st == VS_RETURNING) {
            if (after(now, v->t0 + LINK_RESTORE_MS)) v->st = VS_FREE;
            else if (after(now, v->sent + LINK_RETURN_RESEND_MS)) { send_return(L, v); v->sent = now; }
        }
    }
}
