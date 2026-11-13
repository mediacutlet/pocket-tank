/* piranha_spike.c - THE PIRANHA SPIKE (2026-11-08, docs/piranha-tank.md - THROWAWAY, never released).
 * See piranha_spike.h. Built the stingray spike's way (ray-spike-041 ray_spike.c): the room and the
 * skin baked once, the bodies as small lit meshes, a fixed-point edge-walk raster, every motion on
 * springs (heading, pitch, speed) and the body moving only along its nose.
 *
 * The world: x right, y DOWN (the screen's), z away from the front glass (0) to the back (ZD).
 * The camera looks straight in; a point at depth z is drawn scaled by F/(F+z) about (CX, CY), so
 * the floor and the underside of the surface show as bands and far things sit smaller. */
#include "piranha_spike.h"
#include <math.h>
#include <string.h>

#ifndef PT_FB_SWAP
#define PT_FB_SWAP 0
#endif
#define FBX(v) (PT_FB_SWAP ? (uint16_t)((((uint16_t)(v)) << 8) | (((uint16_t)(v)) >> 8)) : (uint16_t)(v))

#define PI 3.14159265f
#define WMAX 480
#define HMAX 502
#define TEXW 128
#define TEXH 64
#define NFISH_MAX 12
#define NC 10                    /* flank columns, tail to snout */
#define NU 3                     /* rows above the mouth line */
#define NL 2                     /* rows below it (the lower jaw's part) */
#define MAXV 360
#define MAXT 460
#define NBUCK 40
#define MAXPUFF 140
#define CG 4                     /* the cloud's lattice: one point every 4 px */
#define LW (WMAX / CG + 2)
#define LH (HMAX / CG + 2)
#define MAXBIT 24
#define MAXMEAT 3
#define NMOTE 70
#define MAXBUB 40
#define MAXRING 6
#define MAXROOT 40
#define OVMAX 60000
#define RUNMAX 6000

/* ------------------------------------------------------------------ small maths (the ray spike's) */
typedef struct { float x, y, z; } v3;
static inline v3 V(float x, float y, float z) { v3 r = { x, y, z }; return r; }
static inline v3 add(v3 a, v3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 sub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 mul(v3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static inline float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3 cross(v3 a, v3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float rsq(float x) {
    union { float f; uint32_t i; } u = { x }; u.i = 0x5f3759dfu - (u.i >> 1);
    float y = u.f; y = y * (1.5f - 0.5f * x * y * y); return y * (1.5f - 0.5f * x * y * y);
}
static inline float fsqrt(float x) { return x > 1e-12f ? x * rsq(x) : 0; }
static inline v3 norm(v3 a) { float l = dot(a, a); return l > 1e-12f ? mul(a, rsq(l)) : V(0, -1, 0); }
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline v3 mix3(v3 a, v3 b, float t) { return V(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline float smooth(float e0, float e1, float x) { float t = clampf((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); }
static inline float fmaxf_(float a, float b) { return a > b ? a : b; }

#define T_LAP 62831.853f
#define SINN 1024
static float *s_sin;                          /* the table lives in the spike's own (PSRAM) block: internal RAM is tight */
static void sin_init(float *tab) { for (int i = 0; i < SINN; i++) tab[i] = sinf(2 * PI * i / SINN); s_sin = tab; }
static inline float fsin(float x) {
    float f = x * (SINN / (2 * PI)) + (float)(SINN * 4096);
    int i = (int)f; float t = f - (float)i;
    float a = s_sin[i & (SINN - 1)], b = s_sin[(i + 1) & (SINN - 1)];
    return a + (b - a) * t;
}
static inline float fcos(float x) { return fsin(x + PI * 0.5f); }
static inline float fexpn(float x) {           /* e^-x for x >= 0 */
    if (x > 8) return 0;
    if (x < 0) x = 0;
    float y = 1 + x * (1.0f / 16); y *= y; y *= y; y *= y; y *= y;
    return 1.0f / y;
}
static inline uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16);
}
static inline float hf(int x, int y) { return (hash2(x, y) & 0xffffff) / 16777216.0f; }
static float vnoise(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y); float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hf(xi, yi), b = hf(xi + 1, yi), c = hf(xi, yi + 1), d = hf(xi + 1, yi + 1);
    return mixf(mixf(a, b, fx), mixf(c, d, fx), fy);
}
static float fbm(float x, float y) {
    return (0.5f * vnoise(x, y) + 0.25f * vnoise(2.03f * x + 5.2f, 2.03f * y + 1.3f) + 0.125f * vnoise(4.1f * x + 9.1f, 4.1f * y + 3.7f)) / 0.875f;
}
static inline uint16_t pack565(float r, float g, float b) {
    int R = (int)(clampf(r, 0, 1) * 31 + 0.5f), G = (int)(clampf(g, 0, 1) * 63 + 0.5f), B = (int)(clampf(b, 0, 1) * 31 + 0.5f);
    return (uint16_t)((R << 11) | (G << 5) | B);
}
static inline v3 unpack565(uint16_t p) { return V(((p >> 11) & 31) * (1.0f / 31), ((p >> 5) & 63) * (1.0f / 63), (p & 31) * (1.0f / 31)); }
static inline uint16_t pack565d(float r, float g, float b, int x, int y) {
    static const float BY[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    float d = (BY[(y & 3) * 4 + (x & 3)] + 0.5f) / 16 - 0.5f;
    int R = (int)(clampf(r, 0, 1) * 31 + 0.5f + d), G = (int)(clampf(g, 0, 1) * 63 + 0.5f + d), B = (int)(clampf(b, 0, 1) * 31 + 0.5f + d);
    R = R < 0 ? 0 : R > 31 ? 31 : R; G = G < 0 ? 0 : G > 63 ? 63 : G; B = B < 0 ? 0 : B > 31 ? 31 : B;
    return (uint16_t)((R << 11) | (G << 5) | B);
}
static inline float tone(float v) { return v / (1 + 0.18f * v) * 1.18f; }

/* ------------------------------------------------------------------ the world's shape */
#define F_CAM 520.0f
#define ZD 220.0f
#define KF 0.0085f                /* the murk: how fast things fade with depth */
#define FZ0 26.0f                 /* the shoal's depth range */
#define FZ1 182.0f

/* ------------------------------------------------------------------ types */
enum { K_SKIN, K_FIN, K_SOLID };
enum { S_CALM, S_ALERT, S_APPROACH, S_LUNGE, S_SHAKE, S_RETREAT, S_CIRCLE };
typedef struct { float sx, sy, z, u, v, lr, lg, lb, ar, ag, ab; } rv_t;   /* u..ab: the raster's 8 attributes */
typedef struct { uint16_t a, b, c; uint8_t kind; uint8_t pad; float key; int16_t next; } tri_t;
typedef struct {
    v3 p; float yaw, yv, pit, pv, spd;
    float size, S;
    float tph, tamp, bend, jaw, jaw_to, pect, breath;
    int st; float st_t, st_dur, react;
    v3 off, tp;                      /* the shoal place; the state's own target (retreat, circle) */
    float twitch_in, dart_t, dart_yaw;
    int tk, ti;                      /* target: 0 a meat, 1 a bit; its index */
    float shake, rnd, orbit;
} fish_t;
typedef struct { v3 p, v; float mass, ang, spin, seed; int on, wet, floor; int nh; float hx[8], hy[8], hr[8]; float drip, age; } meat_t;
typedef struct { v3 p, v; float age, life, r0, r1, a0; } puff_t;
typedef struct { v3 p, v; float r, age; int on; } bit_t;
typedef struct { v3 p; float age, r, ph; } bub_t;
typedef struct { float x, z, t0; } ring_t;
typedef struct { v3 a, c, b; float r0, r1, z; uint8_t front, hang; } root_t;
typedef struct { uint32_t off; uint16_t len; uint16_t pad; uint32_t src; } run_t;
typedef struct { uint8_t kind; uint8_t pad; int16_t i; float z; } item_t;

struct piranha_spike_s {
    int w, h; float CX, CY, SURF, FLOOR;
    const uint16_t *scene;
    uint16_t skin[TEXW * TEXH];
    int nf; fish_t fish[NFISH_MAX];
    meat_t meat[MAXMEAT];
    puff_t puff[MAXPUFF]; int npuff;
    bit_t bit[MAXBIT];
    bub_t bub[MAXBUB]; int nbub;
    ring_t ring[MAXRING]; int nring;
    v3 mote[NMOTE], motev[NMOTE];
    /* THE CLOCK (0.4.2 audit FW2-1): the caller's clock is the board's time awake, a double; kept here as a float it gave
     * every frame its rounding for a step (+-20% at a day and a half). T is this tank's own, lapped every T_LAP =
     * 20000 pi s: a whole number of turns of every rate that reads it (0.029 .. 1.6: thousandths), so the lap is seamless */
    float T; int started; double clock0;
    float frenzy, auto_s, auto_t;
    uint32_t rng;
    /* the frame's mesh */
    rv_t vx[MAXV]; int nv;
    tri_t tris[MAXT]; int ntris; int16_t buck[NBUCK];
    /* the front roots, drawn back over the fish */
    run_t run[RUNMAX]; int nrun;
    uint16_t ovc[OVMAX]; uint8_t ova[OVMAX]; int nov;
    /* touch */
    int tdown; float tx0, ty0, tt0, tlx, tly;
    int feed_req; float feed_x[4];
    piranha_spike_stats_t st;
    /* the bake's own buffers */
    root_t root[MAXROOT]; int nroot;
    uint8_t rid[WMAX * HMAX];
    uint8_t rtt[WMAX * HMAX];
    float zrow[HMAX];
    float sintab[SINN];
    item_t items[NFISH_MAX + MAXPUFF + MAXMEAT + MAXBIT];
    v3 grid[(NC + 1) * (NU + 1)];
    float nzs[MAXV];
    v3 vp[MAXV], vn[MAXV], valb[MAXV]; uint8_t vkind[MAXV], vlit[MAXV];   /* lit only when a drawn triangle uses it */
    float l_att, l_fog, l_iv; v3 l_mc; float latd[NC + 1];             /* this fish's light: one depth, one murk */
    uint16_t cloud[2][LW * LH];                                          /* the blood, behind and in front of the shoal (x4096) */
    float cfog[2]; int cn[2];
    int cx0[2], cx1[2], cy0[2], cy1[2];
};

size_t piranha_spike_size(void) { return sizeof(piranha_spike_t); }
static int64_t (*s_now)(void);
static void (*s_yield)(void);
void piranha_spike_set_timer(int64_t (*now_us)(void)) { s_now = now_us; }
void piranha_spike_set_yield(void (*fn)(void)) { s_yield = fn; }
static int64_t now_us(void) { return s_now ? s_now() : 0; }
static float frand(piranha_spike_t *R) { R->rng = R->rng * 1664525u + 1013904223u; return (R->rng >> 8) / 16777216.0f; }
void piranha_spike_get_stats(const piranha_spike_t *R, piranha_spike_stats_t *st) { *st = R->st; }
const uint16_t *piranha_spike_skin(const piranha_spike_t *R, int *w, int *h) { *w = TEXW; *h = TEXH; return R->skin; }

static inline float kz(float z) { return F_CAM / (F_CAM + z); }
static inline void proj(const piranha_spike_t *R, v3 P, float k, float *sx, float *sy) { *sx = R->CX + (P.x - R->CX) * k; *sy = R->CY + (P.y - R->CY) * k; }
static v3 murk(const piranha_spike_t *R, float y) {          /* the water's own colour at a height: tea-dark, a little light from above */
    float t = clampf((y - R->SURF) / (R->FLOOR - R->SURF), 0, 1);
    t = fsqrt(t);
    return mix3(V(0.31f, 0.235f, 0.095f), V(0.024f, 0.019f, 0.011f), t);
}
static inline float fogf(float z) { return 1 - fexpn(z * KF); }

/* ------------------------------------------------------------------ the piranha's shape (body units: X tail..snout -0.30..0.50, Y up) */
static const float PU[9] = { 0.00f, 0.10f, 0.25f, 0.40f, 0.55f, 0.70f, 0.82f, 0.92f, 1.00f };
static const float PTOP[9] = { 0.075f, 0.11f, 0.20f, 0.275f, 0.29f, 0.255f, 0.19f, 0.11f, 0.035f };
static const float PBOT[9] = { -0.075f, -0.12f, -0.23f, -0.30f, -0.31f, -0.275f, -0.21f, -0.14f, -0.075f };
static const float PWID[9] = { 0.028f, 0.045f, 0.072f, 0.092f, 0.095f, 0.088f, 0.074f, 0.055f, 0.035f };
static float ptab(const float *tb, float X) {
    float u = clampf((X + 0.30f) / 0.80f, 0, 1);
    int i = 0; while (i < 7 && u > PU[i + 1]) i++;
    float t = (u - PU[i]) / (PU[i + 1] - PU[i]); t = t * t * (3 - 2 * t);
    return tb[i] + (tb[i + 1] - tb[i]) * t;
}
#define P_TOP(X) ptab(PTOP, X)
#define P_BOT(X) ptab(PBOT, X)
#define P_WID(X) ptab(PWID, X)
#define YM (-0.035f)              /* the mouth line */
#define XH 0.40f                  /* the jaw's hinge */

static v3 skin_at(float X, float Y) {
    float top = P_TOP(X), bot = P_BOT(X);
    float s = clampf((Y - bot) / (top - bot), 0, 1);
    v3 back = V(0.12f, 0.15f, 0.14f), flank = V(0.58f, 0.60f, 0.58f), red = V(0.88f, 0.24f, 0.08f), deep = V(0.66f, 0.12f, 0.05f);
    v3 c = mix3(flank, back, smooth(0.52f, 0.95f, s));
    c = mix3(c, mul(flank, 1.12f), smooth(0.40f, 0.55f, s) * (1 - smooth(0.55f, 0.75f, s)) * 0.6f);   /* the silver band */
    /* the red belly: throat to anal fin, its upper edge wavy; the lower jaw and throat red higher up */
    float edge = 0.40f + 0.05f * fsin(X * 21 + 0.7f) + 0.10f * smooth(0.25f, 0.45f, X) - 0.10f * smooth(-0.05f, -0.28f, X);
    float redness = smooth(edge + 0.04f, edge - 0.10f, s) * smooth(-0.32f, -0.20f, X);
    c = mix3(c, mix3(red, deep, smooth(0.25f, 0.0f, s)), redness * 0.95f);
    /* glitter */
    {   int gx = (int)((X + 0.3f) * 300), gy = (int)((Y + 0.4f) * 300);
        float g = hf(gx >> 1, gy >> 1);
        if (g > 0.88f && s > 0.22f && s < 0.9f) c = mix3(c, V(0.97f, 0.97f, 0.93f), (g - 0.88f) * 5.5f * (1 - redness)); }
    /* scales */
    {   float m = 0.05f * fsin(X * 70 + Y * 70) * fsin(X * 70 - Y * 70);
        c = mul(c, 1 + m); }
    /* the gill cover: a dark edge on an arc, a brassy plate inside it */
    {   float dx = X - 0.43f, dy = Y + 0.02f, d = fsqrt(dx * dx + dy * dy);
        if (X < 0.43f && fabsf(d - 0.15f) < 0.010f && s > 0.08f && s < 0.92f) c = mul(c, 0.55f);
        else if (d < 0.15f) c = V(c.x * 1.06f, c.y * 1.0f, c.z * 0.86f); }
    /* the shoulder spot */
    {   float dx = (X - 0.22f) / 0.06f, dy = (Y - 0.07f) / 0.07f, q = dx * dx + dy * dy;
        if (q < 1) c = mul(c, 1 - 0.45f * (1 - q)); }
    /* the dark band at the tail's root */
    c = mul(c, 1 - 0.45f * smooth(-0.24f, -0.30f, X));
    /* the mouth line, the lips, a nostril */
    if (X > 0.40f && fabsf(Y - YM) < 0.010f) c = V(0.10f, 0.05f, 0.04f);
    else if (X > 0.42f && fabsf(Y - YM) < 0.03f) c = mix3(c, deep, 0.4f);
    {   float dx = X - 0.475f, dy = Y - 0.04f; if (dx * dx + dy * dy < 0.010f * 0.010f) c = V(0.06f, 0.05f, 0.05f); }
    /* the eye */
    {   float ex = 0.395f, ey = 0.085f, dx = X - ex, dy = Y - ey, d = fsqrt(dx * dx + dy * dy);
        if (d < 0.058f) {
            if (d < 0.026f) c = V(0.02f, 0.02f, 0.025f);
            else if (d < 0.045f) { float t = (d - 0.026f) / 0.019f; c = mix3(V(0.85f, 0.42f, 0.10f), V(0.55f, 0.20f, 0.06f), t); }
            else c = mul(c, 0.55f);
            float hx = dx - 0.011f, hy = dy - 0.012f;
            if (hx * hx + hy * hy < 0.009f * 0.009f) c = V(0.97f, 0.96f, 0.92f);
        } }
    return c;
}
static void bake_skin(piranha_spike_t *R) {
    for (int ty = 0; ty < TEXH; ty++) for (int tx = 0; tx < TEXW; tx++) {
        float X = -0.30f + 0.80f * tx / (TEXW - 1), Y = 0.34f - 0.68f * ty / (TEXH - 1);
        float top = P_TOP(X), bot = P_BOT(X);
        float Yc = clampf(Y, bot, top);      /* (outside the outline: the edge's colour, for the bilinear-free edge texels) */
        v3 c = skin_at(X, Yc);
        R->skin[ty * TEXW + tx] = pack565d(c.x, c.y, c.z, tx, ty);
    }
}

/* ------------------------------------------------------------------ the room */
static float shaft_at(float x, float y, float surf) {
    float xp = x + (y - surf) * 0.42f;
    float a = 0.5f + 0.5f * sinf(xp * 0.021f + 1.3f), b = 0.5f + 0.5f * sinf(xp * 0.037f + 4.1f), c = 0.5f + 0.5f * sinf(xp * 0.013f + 2.2f);
    a *= a; a *= a; a *= a; b *= b; b *= b; b *= b; b *= b; c *= c; c *= c;
    return (0.55f * a + 0.35f * b + 0.4f * c) * expf(-(y - surf) * 0.006f);
}
static v3 floor_col(float x, float z) {
    float n = fbm(x * 0.05f, z * 0.08f);
    v3 c = mul(V(0.13f, 0.09f, 0.05f), 0.65f + 0.6f * n);
    /* leaf litter: a leaf per cell, the cells looked at around the point */
    const float CS = 17;
    int cx = (int)floorf(x / CS), cz = (int)floorf(z * 1.5f / CS);
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        uint32_t hsh = hash2(cx + i + 911, cz + j + 377);
        if ((hsh & 7) < 2) continue;
        float lx = (cx + i + ((hsh >> 4) & 255) / 255.0f) * CS, lz = (cz + j + ((hsh >> 12) & 255) / 255.0f) * CS / 1.5f;
        float ang = ((hsh >> 20) & 255) / 255.0f * PI, ca = cosf(ang), sa = sinf(ang);
        float len = 6 + ((hsh >> 3) & 7) * 0.8f, wid = len * 0.32f;
        float dx = x - lx, dz = (z - lz) * 1.5f;
        float a = (dx * ca + dz * sa) / len, b = (-dx * sa + dz * ca) / wid;
        float q = a * a + b * b;
        if (q < 1) {
            int k = (hsh >> 28) & 3;
            v3 lc = k == 0 ? V(0.20f, 0.12f, 0.05f) : k == 1 ? V(0.30f, 0.20f, 0.08f) : k == 2 ? V(0.08f, 0.055f, 0.03f) : V(0.16f, 0.09f, 0.04f);
            if (fabsf(b) < 0.10f) lc = mul(lc, 0.7f);
            lc = mul(lc, 0.85f + 0.3f * (1 - q));
            return lc;
        }
    }
    return c;
}
static v3 surface_col(const piranha_spike_t *R, float x, float z) {
    /* the underside of the surface: mostly the dark water mirrored, with soft glints and floating leaves */
    v3 c = mul(murk(R, R->SURF), 0.95f);
    float rp = fbm(x * 0.045f + 3.1f, z * 0.11f);
    c = add(c, mul(V(0.55f, 0.48f, 0.26f), smooth(0.55f, 0.85f, rp) * 0.45f));
    const float CS = 34;
    int cx = (int)floorf(x / CS), cz = (int)floorf(z / CS);
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        uint32_t hsh = hash2(cx + i + 51, cz + j + 7);
        if ((hsh & 15) > 4) continue;
        float lx = (cx + i + ((hsh >> 4) & 255) / 255.0f) * CS, lz = (cz + j + ((hsh >> 12) & 255) / 255.0f) * CS;
        float ang = ((hsh >> 20) & 255) / 255.0f * PI, ca = cosf(ang), sa = sinf(ang);
        float len = 9 + ((hsh >> 3) & 7), wid = len * 0.4f, dx = x - lx, dz = z - lz;
        float a = (dx * ca + dz * sa) / len, b = (-dx * sa + dz * ca) / wid, q = a * a + b * b;
        if (q < 1) return mul(V(0.05f, 0.04f, 0.02f), 1 + 0.6f * q);
    }
    return c;
}
/* the room's depth along a row (no roots): the surface's underside, the floor, or the back */
static float row_z(const piranha_spike_t *R, int py) {
    float dy = py + 0.5f - R->CY;
    if (py < (int)R->SURF) return 1e9f;                                  /* above the waterline: the air */
    if (dy < -0.01f) { float zs = F_CAM * (R->SURF - R->CY) / dy - F_CAM; if (zs < ZD) return zs < 0 ? 0 : zs; }
    else if (dy > 0.01f) { float zf = F_CAM * (R->FLOOR - R->CY) / dy - F_CAM; if (zf < ZD) return zf < 0 ? 0 : zf; }
    return ZD;
}
static v3 room_at(const piranha_spike_t *R, int px, int py) {
    float dy = py + 0.5f - R->CY;
    v3 c;
    if (py < (int)R->SURF) {                                             /* the air over the water, dark under the canopy */
        float t = py / R->SURF;
        c = mix3(V(0.075f, 0.068f, 0.045f), V(0.035f, 0.03f, 0.02f), t);
        c = mul(c, 0.8f + 0.4f * fbm(px * 0.03f, py * 0.05f));
        return c;
    }
    float z = row_z(R, py), k = kz(z);
    float xw = R->CX + (px + 0.5f - R->CX) / k, yw = R->CY + dy / k;
    float zs = F_CAM * (R->SURF - R->CY) / (dy < -0.01f ? dy : -0.01f) - F_CAM;
    float zf = F_CAM * (R->FLOOR - R->CY) / (dy > 0.01f ? dy : 0.01f) - F_CAM;
    if (dy < -0.01f && zs < ZD) c = surface_col(R, xw, z);
    else if (dy > 0.01f && zf < ZD) {
        if (zf <= 0) {                                                   /* below the floor line at the glass: the bank of mud, cut */
            float d = (py - R->FLOOR);
            c = mul(V(0.10f, 0.07f, 0.04f), 0.6f + 0.6f * fbm(px * 0.09f, d * 0.4f));
            if (hf(px / 3, (int)d / 2) > 0.93f) c = V(0.22f, 0.15f, 0.07f);
            return mul(c, 0.8f);
        }
        c = floor_col(xw, z);
        float lit = 0.9f + 1.6f * shaft_at(xw, R->FLOOR, R->SURF);
        c = mul(c, lit);
    } else {                                                             /* the back: open murk */
        c = mul(murk(R, yw), 0.55f + 0.4f * fbm(xw * 0.015f, yw * 0.02f));
        z = ZD;
    }
    float f = fogf(z);
    v3 mc = murk(R, 0.5f * (yw + (R->CY + dy)));
    c = mix3(c, mc, f);
    /* the light in the water between: shafts from the surface */
    float sh = shaft_at(xw, 0.5f * (yw + R->CY + dy), R->SURF) * clampf(z / 140, 0.15f, 1);
    c = add(c, mul(V(0.80f, 0.72f, 0.38f), sh * 0.16f));
    return c;
}
static inline float vignette(const piranha_spike_t *R, int px, int py) {
    float dx = (px - R->CX) / (R->w * 0.62f), dy = (py - R->h * 0.5f) / (R->h * 0.62f), d = dx * dx + dy * dy;
    return 1 - 0.55f * smooth(0.30f, 1.25f, d);
}

/* the roots: quadratic curves, stamped as discs (nearest last), their normal packed into the scene for the shading pass */
static void root_add(piranha_spike_t *R, v3 a, v3 c, v3 b, float r0, float r1, int front, int hang) {
    if (R->nroot >= MAXROOT) return;
    root_t *q = &R->root[R->nroot++]; q->a = a; q->c = c; q->b = b; q->r0 = r0; q->r1 = r1; q->z = (a.z + b.z + c.z) / 3; q->front = (uint8_t)front; q->hang = (uint8_t)hang;
}
static void make_roots(piranha_spike_t *R) {
    float W = (float)R->w, FL = R->FLOOR, SU = R->SURF;
    /* far trees: prop roots leaving a trunk above the water outward and down, turning to plunge into the mud */
    const float TX[3] = { 0.16f, 0.55f, 0.90f };
    for (int t = 0; t < 3; t++) {
        float z = 176 + 34 * frand(R), x = (TX[t] + 0.08f * (frand(R) - 0.5f)) * W * 1.3f - W * 0.15f;
        v3 top = V(x, -70 - 40 * frand(R), z);
        int n = 3 + (int)(frand(R) * 3);
        for (int i = 0; i < n; i++) {
            float sp = ((i + 0.5f) / n - 0.5f) * 2 * (90 + 60 * frand(R));
            float kn = SU + 20 + 110 * frand(R);                         /* where it turns down */
            v3 end = V(x + sp * (1.15f + 0.2f * frand(R)), FL + 12, z + 24 * (frand(R) - 0.5f));
            v3 ctl = V(x + sp * (0.95f + 0.1f * frand(R)), kn, z + 10 * (frand(R) - 0.5f));
            root_add(R, top, ctl, end, 6.5f + 2 * frand(R), 4.5f + frand(R), 0, 0);
        }
        if (frand(R) < 0.7f) {   /* a hanging aerial root */
            float hx = x + (frand(R) - 0.5f) * 140;
            v3 a = V(hx, -30, z - 6), b = V(hx + (frand(R) - 0.5f) * 20, SU + 50 + 120 * frand(R), z - 6);
            root_add(R, a, V((a.x + b.x) * 0.5f + 6, (a.y + b.y) * 0.5f, z - 6), b, 1.8f, 1.1f, 0, 1);
        }
    }
    /* the front: a tree off the left edge (two prop roots and a hanging one), one root off the right */
    {   v3 top = V(-70, -110, 14);
        root_add(R, top, V(56, SU + 70, 16), V(70, FL + 12, 22), 10.5f, 8.0f, 1, 0);
        root_add(R, top, V(6, SU + 140, 10), V(4, FL + 12, 9), 12.0f, 9.0f, 1, 0);
        root_add(R, V(30, -20, 12), V(34, 100, 12), V(38, 178, 12), 2.4f, 1.5f, 1, 1);
        root_add(R, V(W + 80, -100, 18), V(W - 40, SU + 90, 20), V(W - 50, FL + 12, 24), 9.5f, 7.5f, 1, 0); }
    /* far first */
    for (int i = 1; i < R->nroot; i++) { root_t q = R->root[i]; int j = i - 1; while (j >= 0 && R->root[j].z < q.z) { R->root[j + 1] = R->root[j]; j--; } R->root[j + 1] = q; }
}
static void stamp_roots(piranha_spike_t *R, uint16_t *nbuf) {
    int w = R->w, h = R->h;
    for (int ri = 0; ri < R->nroot; ri++) {
        const root_t *q = &R->root[ri];
        float len = fsqrt(dot(sub(q->c, q->a), sub(q->c, q->a))) + fsqrt(dot(sub(q->b, q->c), sub(q->b, q->c)));
        int n = (int)(len * 2.2f) + 8;
        for (int s = 0; s <= n; s++) {
            float t = (float)s / n, u = 1 - t;
            v3 P = add(add(mul(q->a, u * u), mul(q->c, 2 * u * t)), mul(q->b, t * t));
            P.x += (vnoise(t * len * 0.012f, ri * 5.1f) - 0.5f) * (q->hang ? 5 : 7);   /* (no root is a ruled line) */
            if (P.y > R->FLOOR + 3) continue;
            float r = mixf(q->r0, q->r1, t) * (0.88f + 0.24f * vnoise(t * len * 0.06f, ri * 3.3f));
            float k = kz(P.z), sx = R->CX + (P.x - R->CX) * k, sy = R->CY + (P.y - R->CY) * k, rr = r * k;
            int x0 = (int)(sx - rr - 1), x1 = (int)(sx + rr + 1), y0 = (int)(sy - rr - 1), y1 = (int)(sy + rr + 1);
            if (x1 < 0 || x0 >= w || y1 < 0 || y0 >= h) continue;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > w - 1) x1 = w - 1;
            if (y1 > h - 1) y1 = h - 1;
            uint8_t tt = (uint8_t)((int)(t * len * 1.2f) & 255);
            for (int y = y0; y <= y1; y++) {
                if (R->zrow[y] < P.z - 1) continue;                     /* the floor or the surface is nearer here */
                float dy = (y + 0.5f - sy) / rr;
                for (int x = x0; x <= x1; x++) {
                    float dx = (x + 0.5f - sx) / rr, d2 = dx * dx + dy * dy;
                    if (d2 > 1) continue;
                    int i = y * w + x;
                    R->rid[i] = (uint8_t)(ri + 1); R->rtt[i] = tt;
                    nbuf[i] = (uint16_t)(((uint8_t)(int8_t)(dx * 127)) << 8 | (uint8_t)(int8_t)(dy * 127));
                }
            }
        }
        if (s_yield && (ri & 3) == 3) s_yield();
    }
}
static v3 root_col(const piranha_spike_t *R, int px, int py, int ri, uint8_t tt, uint16_t nb) {
    const root_t *q = &R->root[ri];
    float nx = (int8_t)(nb >> 8) / 127.0f, ny = (int8_t)(nb & 255) / 127.0f, nn = 1 - nx * nx - ny * ny;
    float nz = -fsqrt(nn > 0 ? nn : 0);
    float k = kz(q->z), yw = R->CY + (py + 0.5f - R->CY) / k, xw = R->CX + (px + 0.5f - R->CX) / k;
    v3 L = norm(V(-0.35f, -1.0f, -0.55f));
    float diff = fmaxf_(0, nx * L.x + ny * L.y + nz * L.z);
    float bark = 0.70f + 0.30f * vnoise(tt * 0.30f, nz * 5 + ri * 7.1f) + 0.12f * vnoise(tt * 1.3f, nx * 9 + ri);
    float lent = hf(tt / 2 + ri * 131, (int)((nx + 1) * 6)) > 0.94f ? 0.75f : 1;
    v3 base;
    float wet = yw - R->SURF;
    if (wet < 0) base = V(0.34f, 0.29f, 0.23f);                                   /* dry bark above the water */
    else base = mix3(V(0.36f, 0.28f, 0.19f), V(0.22f, 0.21f, 0.11f), smooth(0, 90, wet));   /* wet, a film of growth lower down */
    if (wet > -4 && wet < 7) base = mul(base, 0.65f);                             /* the wet line */
    float att = wet < 0 ? 0.8f : fexpn(wet * 0.004f) * (0.8f + 0.9f * shaft_at(xw, yw, R->SURF));
    float lit = (0.20f + 1.05f * diff * att + 0.25f * fmaxf_(0, -nx) * att) * bark * lent;
    if (q->hang) lit *= 0.85f;
    v3 c = mul(base, lit);
    if (wet >= 0) {
        float f = fogf(q->z);
        c = mix3(c, murk(R, yw), f);
    } else c = mul(c, 0.75f);
    return c;
}
static void bake_room(piranha_spike_t *R, uint16_t *scene) {
    int w = R->w, h = R->h;
    for (int y = 0; y < h; y++) R->zrow[y] = row_z(R, y);
    memset(R->rid, 0, sizeof R->rid);
    make_roots(R);
    stamp_roots(R, scene);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            v3 c = R->rid[i] ? root_col(R, x, y, R->rid[i] - 1, R->rtt[i], scene[i]) : room_at(R, x, y);
            c = mul(c, vignette(R, x, y));
            scene[i] = pack565d(tone(c.x), tone(c.y), tone(c.z), x, y);   /* (native for now) */
        }
        if (s_yield && (y & 7) == 7) s_yield();
    }
    /* soften the roots' edges: a pixel whose neighbour is another thing takes a little of it */
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int i = y * w + x, r = R->rid[i];
            int e = (R->rid[i - 1] != r) + (R->rid[i + 1] != r) + (R->rid[i - w] != r) + (R->rid[i + w] != r);
            if (!e || !r) continue;
            v3 c = mul(unpack565(scene[i]), 2);
            c = add(c, add(add(unpack565(scene[i - 1]), unpack565(scene[i + 1])), add(unpack565(scene[i - w]), unpack565(scene[i + w]))));
            scene[i] = pack565(c.x / 6, c.y / 6, c.z / 6);
            R->rtt[i] = 0xff;                                   /* (marks an edge for the front runs) */
        }
    }
    /* the waterline on the front glass */
    {   int y = (int)R->SURF;
        for (int x = 0; x < w; x++) {
            v3 a = unpack565(scene[(y - 1) * w + x]), b = unpack565(scene[y * w + x]);
            float vg = vignette(R, x, y);
            scene[(y - 1) * w + x] = pack565(a.x + 0.30f * vg, a.y + 0.28f * vg, a.z + 0.18f * vg);
            scene[y * w + x] = pack565(b.x * 0.7f, b.y * 0.7f, b.z * 0.7f);
        } }
    /* the front roots as runs (their pixels, edge pixels at half) */
    R->nrun = 0; R->nov = 0;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < w) {
            int i = y * w + x, r = R->rid[i];
            if (!r || !R->root[r - 1].front) { x++; continue; }
            int x0 = x;
            while (x < w && R->rid[y * w + x] && R->root[R->rid[y * w + x] - 1].front) x++;
            int len = x - x0;
            if (R->nrun >= RUNMAX || R->nov + len > OVMAX) { x = w; continue; }
            run_t *q = &R->run[R->nrun++]; q->off = (uint32_t)(y * w + x0); q->len = (uint16_t)len; q->src = (uint32_t)R->nov;
            for (int k = 0; k < len; k++) { R->ovc[R->nov] = scene[y * w + x0 + k]; R->ova[R->nov] = R->rtt[y * w + x0 + k] == 0xff ? 150 : 255; R->nov++; }
        }
    }
    for (int i = 0; i < w * h; i++) scene[i] = FBX(scene[i]);
    for (int i = 0; i < R->nov; i++) R->ovc[i] = FBX(R->ovc[i]);
}

/* ------------------------------------------------------------------ build */
static void place_fish(piranha_spike_t *R) {
    for (int i = 0; i < R->nf; i++) {
        fish_t *F = &R->fish[i];
        memset(F, 0, sizeof *F);
        F->size = 0.86f + 0.28f * frand(R);
        F->S = 56 * F->size;
        for (int tries = 0; tries < 30; tries++) {
            F->off = V((frand(R) - 0.5f) * 170, (frand(R) - 0.5f) * 70, (frand(R) - 0.5f) * 110);
            int ok = 1;
            for (int j = 0; j < i; j++) { v3 d = sub(F->off, R->fish[j].off); if (d.x * d.x * 0.6f + d.y * d.y * 2.2f + d.z * d.z * 0.5f < 40 * 40) ok = 0; }
            if (ok) break;
        }
        F->p = add(V(R->CX, R->h * 0.56f, 100), F->off);
        F->yaw = frand(R) < 0.7f ? 0.1f * (frand(R) - 0.5f) : PI + 0.1f * (frand(R) - 0.5f);
        F->tph = frand(R) * 6.28f; F->pect = frand(R) * 6.28f; F->breath = frand(R) * 6.28f;
        F->rnd = frand(R); F->twitch_in = 4 + 14 * frand(R);
        F->orbit = frand(R) < 0.5f ? 1 : -1;
        F->st = S_CALM;
    }
}
void piranha_spike_build(piranha_spike_t *R, uint16_t *scene, int w, int h, int n_fish) {
    memset(R, 0, sizeof *R);
    sin_init(R->sintab);
    R->w = w > WMAX ? WMAX : w; R->h = h > HMAX ? HMAX : h;
    R->CX = R->w * 0.5f; R->CY = R->h * 0.45f; R->SURF = (float)(int)(R->h * 0.072f); R->FLOOR = R->h - 14.0f;
    R->rng = 0x9e3779b9u;
    R->nf = n_fish < 1 ? 1 : n_fish > NFISH_MAX ? NFISH_MAX : n_fish;
    bake_skin(R);
    bake_room(R, scene);
    R->scene = scene;
    place_fish(R);
    for (int i = 0; i < NMOTE; i++) {
        R->mote[i] = V(frand(R) * R->w, R->SURF + 6 + frand(R) * (R->FLOOR - R->SURF - 10), 4 + frand(R) * (ZD - 10));
        R->motev[i] = V((frand(R) - 0.5f) * 3, (frand(R) - 0.4f) * 2, 0);
    }
}

/* ------------------------------------------------------------------ food, blood */
void piranha_spike_feed(piranha_spike_t *R, float x) { if (R->feed_req < 4) R->feed_x[R->feed_req++] = x; }
void piranha_spike_auto(piranha_spike_t *R, float every_s) { R->auto_s = every_s; R->auto_t = every_s > 0 ? 3 : 0; }
void piranha_spike_touch(piranha_spike_t *R, float x, float y, bool down) {
    if (down && !R->tdown) { R->tdown = 1; R->tx0 = x; R->ty0 = y; R->tt0 = R->T; }
    if (down) { R->tlx = x; R->tly = y; }
    if (!down && R->tdown) {
        R->tdown = 0;
        float dx = R->tlx - R->tx0, dy = R->tly - R->ty0;
        if (dx * dx + dy * dy < 24 * 24 && R->T - R->tt0 < 0.8f) piranha_spike_feed(R, R->tx0);
    }
}
static void drop_meat(piranha_spike_t *R, float sx) {
    for (int i = 0; i < MAXMEAT; i++) {
        meat_t *m = &R->meat[i];
        if (m->on) continue;
        memset(m, 0, sizeof *m);
        float z = 40 + 55 * frand(R), k = kz(z);
        float x = R->CX + (clampf(sx, 40, R->w - 40) - R->CX) / k;
        m->p = V(x, -18, z); m->v = V((frand(R) - 0.5f) * 10, 40, 0);
        m->mass = 1; m->ang = frand(R) * 6.28f; m->spin = (frand(R) - 0.5f) * 2.5f; m->seed = frand(R) * 10;
        m->on = 1;
        return;
    }
}
static inline float meat_r(const meat_t *m) { return 3 + 9.5f * fsqrt(m->mass); }
static void puff_add(piranha_spike_t *R, v3 p, v3 v, float r0, float r1, float a0, float life) {
    int i = R->npuff < MAXPUFF ? R->npuff++ : 0;
    if (i == 0 && R->npuff == MAXPUFF) {   /* full: the oldest goes */
        float best = -1; for (int k = 0; k < MAXPUFF; k++) if (R->puff[k].age / R->puff[k].life > best) { best = R->puff[k].age / R->puff[k].life; i = k; }
    }
    puff_t *q = &R->puff[i]; q->p = p; q->v = v; q->age = 0; q->life = life; q->r0 = r0; q->r1 = r1; q->a0 = a0;
}
static void bit_add(piranha_spike_t *R, v3 p, v3 v, float r) {
    for (int i = 0; i < MAXBIT; i++) if (!R->bit[i].on) { bit_t *b = &R->bit[i]; b->p = p; b->v = v; b->r = r; b->age = 0; b->on = 1; return; }
}
static void bub_add(piranha_spike_t *R, v3 p, float r) {
    if (R->nbub >= MAXBUB) return;
    bub_t *b = &R->bub[R->nbub++]; b->p = p; b->age = 0; b->r = r; b->ph = frand(R) * 6.28f;
}
static void ring_add(piranha_spike_t *R, float x, float z) {
    if (R->nring >= MAXRING) { memmove(R->ring, R->ring + 1, sizeof(ring_t) * (MAXRING - 1)); R->nring--; }
    ring_t *g = &R->ring[R->nring++]; g->x = x; g->z = z; g->t0 = R->T;
}
static void food_tick(piranha_spike_t *R, float dt) {
    for (int i = 0; i < MAXMEAT; i++) {
        meat_t *m = &R->meat[i];
        if (!m->on) continue;
        m->age += dt;
        float r = meat_r(m);
        if (!m->wet) {
            m->v.y += 420 * dt; m->p = add(m->p, mul(m->v, dt));
            if (m->p.y >= R->SURF) {
                m->wet = 1; m->v = mul(m->v, 0.22f);
                ring_add(R, m->p.x, m->p.z);
                for (int k = 0; k < 7; k++) bub_add(R, add(m->p, V((frand(R) - 0.5f) * 10, 4 + frand(R) * 12, (frand(R) - 0.5f) * 6)), 0.8f + frand(R) * 1.3f);
            }
        } else {
            float sink = 5 + 7 * m->mass;                        /* (slow: the frenzy stays in mid-water a while) */
            m->v.x += (0 - m->v.x) * clampf(dt * 1.6f, 0, 1); m->v.z += (0 - m->v.z) * clampf(dt * 1.6f, 0, 1);
            m->v.y += (sink - m->v.y) * clampf(dt * 1.3f, 0, 1);
            m->p = add(m->p, mul(m->v, dt));
            float fy = R->FLOOR - r * 0.7f;
            if (m->p.y > fy) { m->p.y = fy; m->v.y = 0; m->floor = 1; }
            m->p.z = clampf(m->p.z, 20, 170);
            {   float k = kz(m->p.z), half = (R->CX - 24) / k;      /* the sides of the view */
                m->p.x = clampf(m->p.x, R->CX - half, R->CX + half); }
            m->drip -= dt;
            if (m->drip < 0) { m->drip = 0.35f + 0.3f * frand(R); puff_add(R, add(m->p, V(0, -r * 0.3f, 0)), V((frand(R) - 0.5f) * 2, -2, 0), 1.5f, 5 + 4 * m->mass, 0.22f, 6); }
        }
        m->ang += m->spin * dt; m->spin *= fexpn(dt * 0.6f);
        if (m->mass <= 0.03f) {   /* the last of it: a few crumbs */
            for (int k = 0; k < 3; k++) bit_add(R, m->p, V((frand(R) - 0.5f) * 30, (frand(R) - 0.5f) * 20, (frand(R) - 0.5f) * 20), 1.2f + frand(R));
            m->on = 0;
        }
    }
    for (int i = 0; i < MAXBIT; i++) {
        bit_t *b = &R->bit[i];
        if (!b->on) continue;
        b->age += dt;
        b->v = mul(b->v, fexpn(dt * 2.5f)); b->v.y += 9 * dt;
        b->p = add(b->p, mul(b->v, dt));
        if (b->p.y > R->FLOOR - 1) { b->p.y = R->FLOOR - 1; b->v = V(0, 0, 0); }
        if (b->age > 30) b->on = 0;
    }
    int j = 0;
    for (int i = 0; i < R->npuff; i++) {
        puff_t *q = &R->puff[i]; q->age += dt;
        q->v = mul(q->v, fexpn(dt * 1.4f));
        q->p = add(q->p, mul(add(q->v, V(1.6f * fsin(R->T * 0.21f + q->p.y * 0.02f), -1.2f, 0)), dt));
        if (q->age < q->life) R->puff[j++] = *q;
    }
    R->npuff = j;
    j = 0;
    for (int i = 0; i < R->nbub; i++) {
        bub_t *b = &R->bub[i]; b->age += dt;
        b->p.y -= (22 + 10 * b->r) * dt; b->p.x += 6 * fsin(b->ph + b->age * 7) * dt;
        if (b->p.y > R->SURF + 1) R->bub[j++] = *b;
    }
    R->nbub = j;
    for (int i = 0; i < NMOTE; i++) {
        v3 *p = &R->mote[i];
        *p = add(*p, mul(add(R->motev[i], V(1.2f * fsin(R->T * 0.3f + i), 0, 0)), dt));
        if (p->y < R->SURF + 4) p->y = R->FLOOR - 4;
        if (p->y > R->FLOOR - 2) p->y = R->SURF + 6;
        float k = kz(p->z), half = R->CX / k;
        if (p->x < R->CX - half) p->x += 2 * half;
        if (p->x > R->CX + half) p->x -= 2 * half;
    }
}

/* ------------------------------------------------------------------ the shoal */
static void spring(float *x, float *v, float target, float w, float maxv, float maxa, float dt) {
    float a = w * w * (target - *x) - 2 * w * *v;
    a = clampf(a, -maxa, maxa);
    *v = clampf(*v + a * dt, -maxv, maxv);
    *x += *v * dt;
}
static inline float wrapa(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static inline v3 fwd(const fish_t *F) { float cp = fcos(F->pit); return V(cp * fcos(F->yaw), -fsin(F->pit), cp * fsin(F->yaw)); }
static inline v3 mouth_of(const fish_t *F) { return add(F->p, mul(fwd(F), 0.47f * F->S)); }
static int food_live(const piranha_spike_t *R) {
    for (int i = 0; i < MAXMEAT; i++) if (R->meat[i].on && R->meat[i].wet) return 1;
    return 0;
}
static int nearest_meat(const piranha_spike_t *R, v3 p) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MAXMEAT; i++) { const meat_t *m = &R->meat[i]; if (!m->on || !m->wet) continue; v3 d = sub(m->p, p); float q = dot(d, d); if (q < bd) { bd = q; best = i; } }
    return best;
}
static int biters_at(const piranha_spike_t *R, int mi) {
    int n = 0;
    for (int i = 0; i < R->nf; i++) { const fish_t *F = &R->fish[i]; if ((F->st == S_LUNGE || F->st == S_SHAKE) && F->tk == 0 && F->ti == mi) n++; }
    return n;
}
static void set_state(fish_t *F, int st, float dur) { F->st = st; F->st_t = 0; F->st_dur = dur; }
static void bite(piranha_spike_t *R, fish_t *F) {
    v3 mo = mouth_of(F);
    if (F->tk == 0) {
        meat_t *m = &R->meat[F->ti];
        if (!m->on) return;
        float r = meat_r(m);
        m->mass -= 0.022f * F->size;
        /* the hole, where the mouth met it (in the meat's own turn) */
        float a = atan2f(mo.y - m->p.y, mo.x - m->p.x) - m->ang;
        int k = m->nh < 8 ? m->nh++ : (int)(frand(R) * 8);
        m->hx[k] = 0.95f * cosf(a); m->hy[k] = 0.95f * sinf(a); m->hr[k] = 0.38f + 0.12f * frand(R);
        v3 pull = norm(sub(F->p, m->p));
        m->v = add(m->v, mul(pull, 22 + 14 * frand(R))); m->spin += (frand(R) - 0.5f) * 5;
        for (int q = 0; q < 4; q++)
            puff_add(R, add(mo, V((frand(R) - 0.5f) * 8, (frand(R) - 0.5f) * 8, (frand(R) - 0.5f) * 8)),
                     V((frand(R) - 0.5f) * 22, (frand(R) - 0.6f) * 16, (frand(R) - 0.5f) * 14), 2 + 2 * frand(R), 14 + 16 * frand(R), 0.22f + 0.10f * frand(R), 13 + 10 * frand(R));
        if (frand(R) < 0.7f) bit_add(R, add(m->p, mul(pull, -r * 0.5f)), V((frand(R) - 0.5f) * 50, -10 - 20 * frand(R), (frand(R) - 0.5f) * 40), 1.0f + 1.2f * frand(R));
        (void)r;
    } else {
        bit_t *b = &R->bit[F->ti];
        if (!b->on) return;
        b->on = 0;
        puff_add(R, mo, V(0, -2, 0), 1.5f, 6, 0.25f, 6);
    }
}
static void fish_tick(piranha_spike_t *R, fish_t *F, int idx, float dt) {
    const float S = F->S;
    int food = food_live(R);
    v3 T = F->p; float Vw = 0, w = 2.0f, maxv = 1.3f, maxa = 3.0f, acc = 30; int flat = 1, face_ok = 0; float face = 0;
    F->st_t += dt;
    int mi = nearest_meat(R, F->p);
    /* a crumb near the mouth beats the big piece */
    int bi = -1; float bd = (1.6f * S) * (1.6f * S);
    if (food || R->frenzy > 0.2f) for (int i = 0; i < MAXBIT; i++) if (R->bit[i].on) { v3 d = sub(R->bit[i].p, F->p); float q = dot(d, d); if (q < bd) { bd = q; bi = i; } }
    switch (F->st) {
    case S_CALM: {
        float t = R->T;
        v3 anc = V(R->CX + R->w * 0.20f * fsin(t * 0.045f + 1) + R->w * 0.06f * fsin(t * 0.13f), R->h * 0.56f + R->h * 0.07f * fsin(t * 0.07f), 100 + 40 * fsin(t * 0.029f));
        float adx = R->w * 0.20f * 0.045f * fcos(t * 0.045f + 1) + R->w * 0.06f * 0.13f * fcos(t * 0.13f);
        T = add(anc, F->off);
        v3 d = sub(T, F->p); float dist = fsqrt(dot(d, d));
        float ag = 1 + 1.5f * R->frenzy;
        Vw = (dist > 30 ? clampf(dist * 0.35f, 6, 26) : 2 + dist * 0.12f) * ag;
        face = adx >= 0 ? 0.12f * fsin(t * 0.11f + F->rnd * 3) : PI + 0.12f * fsin(t * 0.11f + F->rnd * 3);
        face_ok = dist < 40;
        w = 1.6f * ag; maxv = 1.1f * ag; maxa = 2.4f * ag * ag; acc = 20 * ag;
        F->twitch_in -= dt * (1 + 3 * R->frenzy);
        if (F->twitch_in <= 0) { F->twitch_in = 6 + 16 * frand(R); F->dart_t = 0.30f + 0.2f * frand(R); F->dart_yaw = F->yaw + (frand(R) < 0.5f ? -1 : 1) * (0.5f + 0.7f * frand(R)); }
        if (F->dart_t > 0) { F->dart_t -= dt; v3 h = V(fcos(F->dart_yaw), 0, fsin(F->dart_yaw) * 0.5f); T = add(F->p, mul(h, 100)); Vw = 55; w = 7; maxv = 6; maxa = 60; acc = 300; face_ok = 0; }
        F->jaw_to = 0.04f + 0.04f * (0.5f + 0.5f * fsin(F->breath));
        if (food) { set_state(F, S_ALERT, 0); F->react = 0.05f + (mi >= 0 ? fsqrt(dot(sub(R->meat[mi].p, F->p), sub(R->meat[mi].p, F->p))) / 380 : 0) + 0.35f * frand(R); }
        break; }
    case S_ALERT:
        if (mi < 0) { set_state(F, S_CALM, 0); break; }
        T = R->meat[mi].p; Vw = 8; w = 6; maxv = 5; maxa = 40; acc = 120; flat = 0;
        if (F->st_t > F->react) set_state(F, S_APPROACH, 0);
        break;
    case S_APPROACH: {
        if (mi < 0 && bi < 0) { set_state(F, S_CALM, 0); break; }
        flat = 0; w = 6.5f; maxv = 6.5f; maxa = 55; acc = 380;
        if (bi >= 0 && (mi < 0 || bd < dot(sub(R->meat[mi].p, F->p), sub(R->meat[mi].p, F->p)))) {
            bit_t *b = &R->bit[bi];
            T = sub(b->p, mul(fwd(F), 0.45f * S)); Vw = 90;
            if (fsqrt(dot(sub(mouth_of(F), b->p), sub(mouth_of(F), b->p))) < 0.30f * S) { F->tk = 1; F->ti = bi; set_state(F, S_LUNGE, 0.12f); }
            break;
        }
        meat_t *m = &R->meat[mi];
        float r = meat_r(m);
        v3 away = norm(sub(F->p, m->p));
        if (fabsf(away.y) > 0.7f) away = norm(V(away.x + 0.5f * F->orbit, away.y * 0.4f, away.z));
        v3 bp = add(m->p, mul(away, r + 0.45f * S));
        T = bp; Vw = 80 + 40 * F->rnd;
        v3 dm = sub(mouth_of(F), m->p);
        float md = fsqrt(dot(dm, dm));
        if (md < r + 0.30f * S) {
            if (biters_at(R, mi) < 3) { F->tk = 0; F->ti = mi; set_state(F, S_LUNGE, 0.16f); }
            else { set_state(F, S_CIRCLE, 0.5f + 0.8f * frand(R)); F->ti = mi; }
        }
        break; }
    case S_LUNGE: {
        v3 tp = F->tk == 0 ? (R->meat[F->ti].on ? R->meat[F->ti].p : F->p) : (R->bit[F->ti].on ? R->bit[F->ti].p : F->p);
        T = tp; Vw = 140; w = 11; maxv = 9; maxa = 120; acc = 900; flat = 0;
        F->jaw_to = 1;
        float rr = F->tk == 0 && R->meat[F->ti].on ? meat_r(&R->meat[F->ti]) : 1;
        v3 dm = sub(mouth_of(F), tp);
        if (F->st_t > F->st_dur || dot(dm, dm) < (rr + 2) * (rr + 2)) {
            bite(R, F);
            F->jaw = 0.15f; F->jaw_to = 0;
            set_state(F, F->tk == 0 ? S_SHAKE : S_RETREAT, F->tk == 0 ? 0.28f + 0.2f * frand(R) : 0.4f);
            if (F->tk != 0) { v3 aw = norm(V(-fwd(F).x, 0.2f * (frand(R) - 0.5f), -fwd(F).z)); F->tp = add(F->p, mul(aw, 60)); }
        }
        break; }
    case S_SHAKE: {
        flat = 0; Vw = -18; T = add(F->p, mul(fwd(F), 50)); w = 4; maxv = 3; maxa = 30; acc = 300;
        F->jaw_to = 0;
        F->shake = 0.42f * fsin(F->st_t * 2 * PI * 8.5f) * (1 - F->st_t / F->st_dur);
        if (F->st_t > F->st_dur) {
            F->shake = 0;
            v3 aw = F->tk == 0 && R->meat[F->ti].on ? norm(sub(F->p, R->meat[F->ti].p)) : mul(fwd(F), -1);
            v3 side = norm(cross(aw, V(0, 1, 0)));
            F->tp = add(F->p, add(mul(aw, 70), mul(side, 50 * (frand(R) - 0.5f))));
            set_state(F, S_RETREAT, 0.45f + 0.7f * frand(R));
        }
        break; }
    case S_RETREAT:
        flat = 0; T = F->tp; Vw = 70; w = 6; maxv = 6; maxa = 50; acc = 350;
        F->jaw_to = 0.30f * (0.5f + 0.5f * fsin(F->st_t * 17));      /* chewing */
        if (F->st_t > F->st_dur) set_state(F, food || bi >= 0 ? S_APPROACH : S_CALM, 0);
        break;
    case S_CIRCLE: {
        if (mi < 0) { set_state(F, S_CALM, 0); break; }
        meat_t *m = &R->meat[mi];
        float r = meat_r(m) + 1.2f * S, a = R->T * 1.6f * F->orbit + F->rnd * 6.28f;
        T = add(m->p, V(r * fcos(a), 0.4f * r * fsin(a * 0.7f), 0.8f * r * fsin(a)));
        Vw = 60; w = 5; maxv = 5; maxa = 40; acc = 250; flat = 0;
        if (F->st_t > F->st_dur) set_state(F, S_APPROACH, 0);
        break; }
    }
    /* steering: toward T, apart from the others, inside the view */
    v3 D = sub(T, F->p);
    if (flat) D.z *= 0.45f;                     /* (calm: mostly side on, the way a shoal hangs in a tank) */
    float dl = fsqrt(dot(D, D)); if (dl > 1e-3f) D = mul(D, 1 / dl);
    for (int j = 0; j < R->nf; j++) {
        if (j == idx) continue;
        const fish_t *G = &R->fish[j];
        v3 d = sub(F->p, G->p); d.y *= 1.6f;
        float q = dot(d, d), rr = 0.62f * (S + G->S) * (F->st == S_LUNGE ? 0.5f : 1);
        if (q < rr * rr && q > 1e-3f) { float l = fsqrt(q); D = add(D, mul(d, (rr - l) / rr / l * 1.6f)); }
    }
    {   float k = kz(F->p.z), half = (R->CX - 26) / k;
        float x0 = R->CX - half, x1 = R->CX + half, y0 = R->SURF + 0.5f * S, y1 = R->FLOOR - 0.35f * S;
        if (F->p.x < x0 + 30) D.x += (x0 + 30 - F->p.x) * 0.05f;
        if (F->p.x > x1 - 30) D.x -= (F->p.x - x1 + 30) * 0.05f;
        if (F->p.y < y0 + 15) D.y += (y0 + 15 - F->p.y) * 0.06f;
        if (F->p.y > y1 - 10) D.y -= (F->p.y - y1 + 10) * 0.06f;
        if (F->p.z < FZ0 + 20) D.z += (FZ0 + 20 - F->p.z) * 0.05f;
        if (F->p.z > FZ1 - 20) D.z -= (F->p.z - FZ1 + 20) * 0.05f; }
    float dyaw = atan2f(D.z, D.x);
    if (face_ok) {   /* hanging in the shoal: everyone faces the same way */
        float b = smooth(40, 10, dl);
        dyaw = atan2f((1 - b) * fsin(dyaw) + b * fsin(face), (1 - b) * fcos(dyaw) + b * fcos(face));
    }
    float err = wrapa(dyaw - F->yaw);
    spring(&F->yaw, &F->yv, F->yaw + err, w, maxv, maxa, dt);
    F->yaw = wrapa(F->yaw);
    float hz = fsqrt(D.x * D.x + D.z * D.z);
    float dp = clampf(atan2f(-D.y, hz > 1e-3f ? hz : 1e-3f), -0.6f, 0.6f) * (flat ? 0.6f : 1);
    spring(&F->pit, &F->pv, dp, w * 0.8f, maxv, maxa, dt);
    if (F->st != S_LUNGE && F->st != S_SHAKE) Vw *= 0.30f + 0.70f * fcos(err) * fcos(err) * (fabsf(err) < PI * 0.5f ? 1 : 0.2f);
    F->spd += clampf(Vw - F->spd, -acc * dt, acc * dt);
    v3 f = fwd(F);
    F->p = add(F->p, mul(f, F->spd * dt));
    {   float k = kz(F->p.z), half = (R->CX - 10) / k;
        F->p.x = clampf(F->p.x, R->CX - half, R->CX + half);
        F->p.y = clampf(F->p.y, R->SURF + 0.4f * S, R->FLOOR - 0.3f * S);
        F->p.z = clampf(F->p.z, FZ0, FZ1); }
    /* the body: tail beat rides the speed, the bend rides the turn */
    float spdn = fabsf(F->spd);
    float fr = 1.3f + spdn * 0.045f + fabsf(F->yv) * 0.4f;
    F->tph += 2 * PI * fr * dt; if (F->tph > 2000) F->tph -= 2 * PI * 300;
    float at = clampf(0.035f + spdn * 0.0011f + fabsf(F->yv) * 0.018f, 0, 0.16f);
    F->tamp += (at - F->tamp) * clampf(dt * 4, 0, 1);
    float bt = clampf(-F->yv * 0.085f, -0.28f, 0.28f);
    F->bend += (bt - F->bend) * clampf(dt * 8, 0, 1);
    F->pect += dt * 2 * PI * (spdn < 15 ? 2.0f : 0.9f);
    F->breath += dt * 2 * PI * 0.6f;
    float jr = F->jaw_to > F->jaw ? 28 : 40;
    F->jaw += (F->jaw_to - F->jaw) * clampf(dt * jr, 0, 1);
}

/* ------------------------------------------------------------------ meshing + light */
typedef struct { v3 p, f, u, l; float S, k, yaw, cyaw, syaw; } frame_t;
static inline float lat_d(const fish_t *F, float X) {           /* the body's sideways line, in body units */
    float e = 0.5f - X, env = 0.12f + 0.88f * e * e;
    float d = F->tamp * env * fsin(F->tph + X * 5.8f);
    d += F->bend * (X - 0.05f) * (X - 0.05f) * 1.2f;
    return d;
}
static inline v3 body_pt_d(const frame_t *B, float X, float Y, float Z, float d) {
    return add(B->p, add(mul(B->f, X * B->S), add(mul(B->u, Y * B->S), mul(B->l, (Z + d) * B->S))));
}
static inline v3 body_pt(const frame_t *B, const fish_t *F, float X, float Y, float Z) { return body_pt_d(B, X, Y, Z, lat_d(F, X)); }
static void fish_light_consts(piranha_spike_t *R, const fish_t *F) {
    float depth = F->p.y - R->SURF;
    R->l_att = fexpn(depth * 0.0035f) * (0.75f + 0.6f * (0.5f + 0.5f * fsin(F->p.x * 0.021f + F->p.y * 0.009f + 1.3f)));
    R->l_fog = fogf(F->p.z); R->l_iv = 1 - R->l_fog; R->l_mc = murk(R, F->p.y);
}
/* the screen place now; the light later, and only if a drawn triangle needs it */
static void put_vertex(piranha_spike_t *R, int vi, v3 P, v3 N, v3 alb, float k, int kind) {
    rv_t *o = &R->vx[vi];
    proj(R, P, k, &o->sx, &o->sy); o->z = P.z;
    R->vp[vi] = P; R->vn[vi] = N; R->valb[vi] = alb; R->vkind[vi] = (uint8_t)kind; R->vlit[vi] = 0;
}
static void light_vertex(piranha_spike_t *R, int vi) {
    rv_t *o = &R->vx[vi];
    v3 N = R->vn[vi], alb = R->valb[vi]; int kind = R->vkind[vi];
    static const v3 LD = { 0.15f, -0.94f, -0.30f }, HH = { 0.08f, -0.55f, -0.83f };
    float nl = dot(N, LD), nh = dot(N, HH);
    if (kind == K_FIN) { nl = fabsf(nl); nh = fabsf(nh); }
    float diff = fmaxf_(0, nl) * 0.80f * R->l_att;
    float L = 0.26f + 0.16f * (-N.y) + diff + fmaxf_(0, -N.z) * 0.18f;
    float sp = 0;
    if (kind == K_SKIN) { float q = fmaxf_(0, nh); q *= q; q *= q; q *= q; sp = q * q * 0.55f * R->l_att; }   /* ^16: the silver */
    float iv = R->l_iv, fog = R->l_fog; v3 mc = R->l_mc;
    o->lr = L * alb.x * iv * (256.0f / 31); o->lg = L * alb.y * iv * (256.0f / 63); o->lb = L * alb.z * iv * (256.0f / 31);
    o->ar = (sp * iv + mc.x * fog) * 256; o->ag = (sp * iv + mc.y * fog) * 256; o->ab = (sp * 0.95f * iv + mc.z * fog) * 256;
    R->vlit[vi] = 1;
}
static int vtx(piranha_spike_t *R) { return R->nv < MAXV ? R->nv++ : MAXV - 1; }
static void add_tri(piranha_spike_t *R, int a, int b, int c, int kind, float bias) {
    if (R->ntris >= MAXT) return;
    rv_t *A = &R->vx[a], *B = &R->vx[b], *C = &R->vx[c];
    float area = (B->sx - A->sx) * (C->sy - A->sy) - (B->sy - A->sy) * (C->sx - A->sx);
    if (area > -0.02f && area < 0.02f) return;
    tri_t *t = &R->tris[R->ntris++];
    t->a = (uint16_t)a; t->b = (uint16_t)b; t->c = (uint16_t)c; t->kind = (uint8_t)kind;
    t->key = (A->z + B->z + C->z) * (1.0f / 3) + bias;
}
/* a flank: one side's grid, upper part (mouth line to back) and lower part (belly to mouth line, the jaw) */
static void flank(piranha_spike_t *R, const frame_t *B, const fish_t *F, int side, float jaw_a, int *base_u, int *base_l, float *nzs) {
    const float sd = side ? 1.0f : -1.0f;
    float cja = fcos(jaw_a), sja = fsin(jaw_a);
    for (int part = 0; part < 2; part++) {
        int nr = part == 0 ? NU : NL;
        int b0 = R->nv;
        if (part == 0) *base_u = b0; else *base_l = b0;
        v3 *P = R->grid;
        for (int i = 0; i <= NC; i++) {
            float X = -0.30f + 0.80f * i / NC, top = P_TOP(X), bot = P_BOT(X), c = 0.5f * (top + bot), hh = 0.5f * (top - bot), W = P_WID(X);
            float wj = part == 1 ? smooth(XH - 0.05f, XH + 0.03f, X) : 0;
            for (int j = 0; j <= nr; j++) {
                float t = (float)j / nr, g = fsin(t * PI * 0.5f);
                float Y = part == 0 ? YM + (top - YM) * g : YM - (YM - bot) * g;
                float q = (Y - c) / hh, ww = 1 - q * q;
                float Z = sd * W * fsqrt(ww > 0 ? ww : 0);
                float Xr = X, Yr = Y;
                if (wj > 0) {                                     /* the lower jaw swings down on its hinge */
                    float a = jaw_a * wj, ca = a == jaw_a ? cja : fcos(a), sa = a == jaw_a ? sja : fsin(a);
                    float dx = X - XH, dy = Y - YM;
                    Xr = XH + dx * ca + dy * sa; Yr = YM - dx * sa + dy * ca;
                }
                P[i * (nr + 1) + j] = body_pt_d(B, Xr, Yr, Z, R->latd[i]);
                rv_t *o = &R->vx[vtx(R)];
                o->u = (X + 0.30f) / 0.80f * (TEXW - 1);
                o->v = (0.34f - Y) / 0.68f * (TEXH - 1);
                (void)o;
            }
        }
        /* normals from the grid, pointing out of this side */
        for (int i = 0; i <= NC; i++) for (int j = 0; j <= nr; j++) {
            int i0 = i > 0 ? i - 1 : i, i1 = i < NC ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < nr ? j + 1 : j;
            v3 du = sub(P[i1 * (nr + 1) + j], P[i0 * (nr + 1) + j]), dv = sub(P[i * (nr + 1) + j1], P[i * (nr + 1) + j0]);
            v3 N = norm(cross(du, dv));
            float t = (float)j / nr, rad = part == 0 ? t : -t;
            v3 ref = add(mul(B->l, sd), mul(B->u, rad * 1.4f));
            if (dot(N, ref) < 0) N = mul(N, -1);
            int vi = b0 + i * (nr + 1) + j;
            {   float u = R->vx[vi].u, v = R->vx[vi].v; put_vertex(R, vi, P[i * (nr + 1) + j], N, V(1, 1, 1), B->k, K_SKIN); R->vx[vi].u = u; R->vx[vi].v = v; }
            nzs[vi] = N.z;
        }
    }
}
static void fin_vtx(piranha_spike_t *R, const frame_t *B, const fish_t *F, float X, float Y, float Z, v3 alb, int kind) {
    int vi = vtx(R);
    v3 P = body_pt(B, F, X, Y, Z);
    put_vertex(R, vi, P, B->l, alb, B->k, kind);
    R->vx[vi].u = R->vx[vi].v = 0;
}
static void mesh_fish(piranha_spike_t *R, const fish_t *F) {
    float *nzs = R->nzs;
    R->nv = 0; R->ntris = 0;
    frame_t B;
    B.S = F->S; B.p = F->p; B.k = kz(F->p.z);
    float yaw = F->yaw + F->shake;
    {   float cp = fcos(F->pit);
        B.f = V(cp * fcos(yaw), -fsin(F->pit), cp * fsin(yaw));
        v3 up = V(0, -1, 0);
        B.u = norm(sub(up, mul(B.f, dot(up, B.f))));
        B.l = cross(B.f, B.u); }
    float jaw_a = F->jaw * 0.62f;
    fish_light_consts(R, F);
    for (int i = 0; i <= NC; i++) R->latd[i] = lat_d(F, -0.30f + 0.80f * i / NC);
    int bu[2], bl[2];
    for (int s = 0; s < 2; s++) flank(R, &B, F, s, jaw_a, &bu[s], &bl[s], nzs);
    for (int s = 0; s < 2; s++) for (int part = 0; part < 2; part++) {
        int nr = part == 0 ? NU : NL, b0 = part == 0 ? bu[s] : bl[s];
        for (int i = 0; i < NC; i++) for (int j = 0; j < nr; j++) {
            int a = b0 + i * (nr + 1) + j, b = a + (nr + 1), c = b + 1, d = a + 1;
            if (nzs[a] + nzs[b] + nzs[c] < 0.05f) add_tri(R, a, b, c, K_SKIN, 0);
            if (nzs[a] + nzs[c] + nzs[d] < 0.05f) add_tri(R, a, c, d, K_SKIN, 0);
        }
    }
    /* the tail fin: a forked sheet */
    {   const v3 C0 = V(0.62f, 0.50f, 0.44f), C1 = V(0.52f, 0.50f, 0.47f), C2 = V(0.10f, 0.09f, 0.085f);
        int b0 = R->nv;
        for (int a = 0; a <= 3; a++) for (int bb = 0; bb <= 4; bb++) {
            float A = a / 3.0f, Bv = (bb - 2) / 2.0f, ab = fabsf(Bv);
            float X = -0.285f - A * (0.15f + 0.08f * ab * ab), Y = Bv * (0.068f + A * (0.15f + 0.02f * A));
            v3 c = A < 0.6f ? mix3(C0, C1, A / 0.6f) : mix3(C1, C2, (A - 0.6f) / 0.4f);
            fin_vtx(R, &B, F, X, Y, 0, c, K_FIN);
        }
        for (int a = 0; a < 3; a++) for (int bb = 0; bb < 4; bb++) {
            int p = b0 + a * 5 + bb;
            add_tri(R, p, p + 5, p + 6, K_FIN, 0); add_tri(R, p, p + 6, p + 1, K_FIN, 0);
        } }
    /* the dorsal, the adipose, the anal and pelvic fins */
    {   const v3 DK = V(0.34f, 0.35f, 0.31f), DE = V(0.20f, 0.20f, 0.18f), RD = V(0.85f, 0.22f, 0.08f), RE = V(0.30f, 0.07f, 0.04f);
        const float DX[3] = { 0.10f, 0.02f, -0.06f }, DTX[3] = { 0.03f, -0.05f, -0.09f }, DTY[3] = { 0.12f, 0.10f, 0.025f };
        int b0 = R->nv;
        for (int i = 0; i < 3; i++) { fin_vtx(R, &B, F, DX[i], P_TOP(DX[i]) - 0.012f, 0, DK, K_FIN); fin_vtx(R, &B, F, DTX[i], P_TOP(DX[i]) + DTY[i], 0, DE, K_FIN); }
        for (int i = 0; i < 2; i++) { int p = b0 + 2 * i; add_tri(R, p, p + 2, p + 3, K_FIN, 0); add_tri(R, p, p + 3, p + 1, K_FIN, 0); }
        b0 = R->nv;
        fin_vtx(R, &B, F, -0.165f, P_TOP(-0.165f) - 0.01f, 0, DK, K_FIN); fin_vtx(R, &B, F, -0.225f, P_TOP(-0.225f) - 0.01f, 0, DK, K_FIN);
        fin_vtx(R, &B, F, -0.235f, P_TOP(-0.2f) + 0.035f, 0, DE, K_FIN);
        add_tri(R, b0, b0 + 1, b0 + 2, K_FIN, 0);
        const float AX[4] = { 0.0f, -0.10f, -0.20f, -0.255f }, ATX[4] = { -0.035f, -0.13f, -0.22f, -0.27f }, ATY[4] = { 0.10f, 0.09f, 0.06f, 0.02f };
        b0 = R->nv;
        for (int i = 0; i < 4; i++) { fin_vtx(R, &B, F, AX[i], P_BOT(AX[i]) + 0.012f, 0, RD, K_FIN); fin_vtx(R, &B, F, ATX[i], P_BOT(AX[i]) - ATY[i], 0, RE, K_FIN); }
        for (int i = 0; i < 3; i++) { int p = b0 + 2 * i; add_tri(R, p, p + 2, p + 3, K_FIN, 0); add_tri(R, p, p + 3, p + 1, K_FIN, 0); }
        b0 = R->nv;
        fin_vtx(R, &B, F, 0.15f, P_BOT(0.15f) + 0.01f, 0, RD, K_FIN); fin_vtx(R, &B, F, 0.09f, P_BOT(0.09f) + 0.01f, 0, RD, K_FIN);
        fin_vtx(R, &B, F, 0.07f, P_BOT(0.12f) - 0.07f, 0, RE, K_FIN);
        add_tri(R, b0, b0 + 1, b0 + 2, K_FIN, 0); }
    /* the pectorals: small paddles off each flank, sculling */
    for (int s = 0; s < 2; s++) {
        float sd = s ? 1.0f : -1.0f, wr = P_WID(0.29f) * 0.75f, fl = 0.06f * (0.6f + 0.4f * fsin(F->pect + s * 1.2f));
        const v3 PC = V(0.72f, 0.52f, 0.36f), PE = V(0.50f, 0.36f, 0.26f);
        int b0 = R->nv;
        fin_vtx(R, &B, F, 0.30f, -0.105f, sd * wr, PC, K_FIN); fin_vtx(R, &B, F, 0.29f, -0.16f, sd * wr, PC, K_FIN);
        fin_vtx(R, &B, F, 0.17f, -0.125f, sd * (wr + fl), PE, K_FIN); fin_vtx(R, &B, F, 0.185f, -0.195f, sd * (wr + fl * 0.8f), PE, K_FIN);
        add_tri(R, b0, b0 + 1, b0 + 3, K_FIN, -0.5f); add_tri(R, b0, b0 + 3, b0 + 2, K_FIN, -0.5f);
    }
    /* the mouth: its inside and the teeth, when it opens */
    if (F->jaw > 0.04f) {
        const v3 MC = V(0.20f, 0.035f, 0.035f), TC = V(0.92f, 0.90f, 0.82f);
        float ca = fcos(jaw_a), sa = fsin(jaw_a);
#define JR(X_, Y_, ox, oy) do { float dx_ = (X_) - XH, dy_ = (Y_) - YM; ox = XH + dx_ * ca + dy_ * sa; oy = YM - dx_ * sa + dy_ * ca; } while (0)
        float l1x, l1y, l2x, l2y;
        JR(0.45f, YM, l1x, l1y); JR(0.515f, YM - 0.015f, l2x, l2y);
        int b0 = R->nv;
        fin_vtx(R, &B, F, XH - 0.02f, YM, 0, MC, K_SOLID); fin_vtx(R, &B, F, 0.45f, YM + 0.004f, 0, MC, K_SOLID); fin_vtx(R, &B, F, 0.505f, YM + 0.006f, 0, MC, K_SOLID);
        fin_vtx(R, &B, F, l1x, l1y, 0, MC, K_SOLID); fin_vtx(R, &B, F, l2x, l2y, 0, MC, K_SOLID);
        add_tri(R, b0, b0 + 1, b0 + 3, K_SOLID, 0); add_tri(R, b0 + 1, b0 + 2, b0 + 4, K_SOLID, 0); add_tri(R, b0 + 1, b0 + 4, b0 + 3, K_SOLID, 0);
        if (F->jaw > 0.12f) for (int i = 0; i < 4; i++) {
            float x = 0.437f + 0.022f * i;
            int t0 = R->nv;
            fin_vtx(R, &B, F, x - 0.010f, YM + 0.004f, 0, TC, K_SOLID); fin_vtx(R, &B, F, x + 0.010f, YM + 0.004f, 0, TC, K_SOLID); fin_vtx(R, &B, F, x, YM - 0.030f, 0, TC, K_SOLID);
            add_tri(R, t0, t0 + 1, t0 + 2, K_SOLID, -0.6f);
            float ax, ay, bx, by, cx, cy;
            JR(x - 0.010f + 0.012f, YM - 0.004f, ax, ay); JR(x + 0.010f + 0.012f, YM - 0.004f, bx, by); JR(x + 0.012f, YM + 0.030f, cx, cy);
            t0 = R->nv;
            fin_vtx(R, &B, F, ax, ay, 0, TC, K_SOLID); fin_vtx(R, &B, F, bx, by, 0, TC, K_SOLID); fin_vtx(R, &B, F, cx, cy, 0, TC, K_SOLID);
            add_tri(R, t0, t0 + 1, t0 + 2, K_SOLID, -0.6f);
        }
#undef JR
    }
}

/* ------------------------------------------------------------------ the raster (the ray spike's) */
static uint8_t s_lut5[768], s_lut6[768];
static int s_lut_ok;
static void lut_init(void) {
    if (s_lut_ok) return;
    for (int i = 0; i < 768; i++) { float v = tone(i / 256.0f); s_lut5[i] = (uint8_t)(clampf(v, 0, 1) * 31 + 0.5f); s_lut6[i] = (uint8_t)(clampf(v, 0, 1) * 63 + 0.5f); }
    s_lut_ok = 1;
}
#define NA8 8
static void __attribute__((noinline)) raster(piranha_spike_t *R, uint16_t *fb, int stride, const tri_t *t, int *px) {
    const rv_t *v[3] = { &R->vx[t->a], &R->vx[t->b], &R->vx[t->c] };
    if (v[1]->sy < v[0]->sy) { const rv_t *q = v[0]; v[0] = v[1]; v[1] = q; }
    if (v[2]->sy < v[1]->sy) { const rv_t *q = v[1]; v[1] = v[2]; v[2] = q; }
    if (v[1]->sy < v[0]->sy) { const rv_t *q = v[0]; v[0] = v[1]; v[1] = q; }
    const rv_t *A = v[0], *B = v[1], *C = v[2];
    float e1x = B->sx - A->sx, e1y = B->sy - A->sy, e2x = C->sx - A->sx, e2y = C->sy - A->sy;
    float area = e1x * e2y - e1y * e2x;
    if (area > -0.02f && area < 0.02f) return;
    float inv = 1.0f / area;
    const float *fa = &A->u, *fb_ = &B->u, *fc = &C->u;
    float gx[NA8], gy[NA8];
    for (int k = 0; k < NA8; k++) {
        float d1 = fb_[k] - fa[k], d2 = fc[k] - fa[k];
        gx[k] = (d1 * e2y - d2 * e1y) * inv; gy[k] = (d2 * e1x - d1 * e2x) * inv;
    }
    int32_t igx[NA8];
    for (int k = 0; k < NA8; k++) igx[k] = (int32_t)(gx[k] * 65536.0f);
    int y0 = (int)(A->sy + 0.5f), y2 = (int)(C->sy + 0.5f);
    if (y0 < 0) y0 = 0;
    if (y2 > R->h) y2 = R->h;
    float dAC = C->sy - A->sy, dAB = B->sy - A->sy, dBC = C->sy - B->sy;
    float sAC = dAC > 1e-4f ? (C->sx - A->sx) / dAC : 0;
    float sAB = dAB > 1e-4f ? (B->sx - A->sx) / dAB : 0;
    float sBC = dBC > 1e-4f ? (C->sx - B->sx) / dBC : 0;
    const uint16_t *tex = t->kind == K_SKIN ? R->skin : NULL;
    const int blend = t->kind == K_FIN;
    int n = 0;
    for (int y = y0; y < y2; y++) {
        float py = y + 0.5f;
        if (py < A->sy || py >= C->sy) continue;
        float xa = A->sx + (py - A->sy) * sAC;
        float xb = py < B->sy ? A->sx + (py - A->sy) * sAB : B->sx + (py - B->sy) * sBC;
        float xl = xa < xb ? xa : xb, xr = xa < xb ? xb : xa;
        int xs = (int)(xl + 0.5f + 1024.0f) - 1024, xe = (int)(xr + 0.5f + 1024.0f) - 1024 - 1;
        if (xs < 0) xs = 0;
        if (xe > R->w - 1) xe = R->w - 1;
        if (xs > xe) continue;
        float dx0 = xs + 0.5f - A->sx, dy0 = py - A->sy;
        int32_t a[NA8];
        for (int k = 0; k < NA8; k++) a[k] = (int32_t)((fa[k] + gx[k] * dx0 + gy[k] * dy0) * 65536.0f);
        uint16_t *row = fb + y * stride;
        for (int x = xs; x <= xe; x++) {
            int r5 = 31, g6 = 63, b5 = 31;
            if (tex) {
                int tu = a[0] >> 16, tv = a[1] >> 16;
                tu = tu < 0 ? 0 : tu > TEXW - 1 ? TEXW - 1 : tu;
                tv = tv < 0 ? 0 : tv > TEXH - 1 ? TEXH - 1 : tv;
                uint16_t p = tex[tv * TEXW + tu];
                r5 = (p >> 11) & 31; g6 = (p >> 5) & 63; b5 = p & 31;
            }
            int cr = (r5 * (a[2] >> 8) >> 8) + (a[5] >> 16), cg = (g6 * (a[3] >> 8) >> 8) + (a[6] >> 16), cb = (b5 * (a[4] >> 8) >> 8) + (a[7] >> 16);
            cr = cr < 0 ? 0 : cr > 767 ? 767 : cr; cg = cg < 0 ? 0 : cg > 767 ? 767 : cg; cb = cb < 0 ? 0 : cb > 767 ? 767 : cb;
            int o5 = s_lut5[cr], o6 = s_lut6[cg], ob = s_lut5[cb];
            if (blend) {                                          /* the fins: three quarters fin, a quarter what is behind */
                uint16_t d = FBX(row[x]);
                o5 = (o5 * 3 + ((d >> 11) & 31)) >> 2; o6 = (o6 * 3 + ((d >> 5) & 63)) >> 2; ob = (ob * 3 + (d & 31)) >> 2;
            }
            row[x] = FBX((o5 << 11) | (o6 << 5) | ob);
            for (int k = 0; k < NA8; k++) a[k] += igx[k];
        }
        n += xe - xs + 1;
    }
    *px += n;
}
static void draw_mesh(piranha_spike_t *R, uint16_t *fb, int stride, float zc, float span, int *px) {
    for (int i = 0; i < R->ntris; i++) { const tri_t *t = &R->tris[i];
        if (!R->vlit[t->a]) light_vertex(R, t->a);
        if (!R->vlit[t->b]) light_vertex(R, t->b);
        if (!R->vlit[t->c]) light_vertex(R, t->c); }
    for (int b = 0; b < NBUCK; b++) R->buck[b] = -1;
    float z0 = zc - span, sc = NBUCK / (2 * span);
    for (int i = 0; i < R->ntris; i++) {
        int b = (int)((R->tris[i].key - z0) * sc);
        b = b < 0 ? 0 : b > NBUCK - 1 ? NBUCK - 1 : b;
        R->tris[i].next = R->buck[b]; R->buck[b] = (int16_t)i;
    }
    for (int b = NBUCK - 1; b >= 0; b--) for (int i = R->buck[b]; i >= 0; i = R->tris[i].next) raster(R, fb, stride, &R->tris[i], px);
}

/* ------------------------------------------------------------------ the cloud, the meat, the little things */
static void blend_disc(const piranha_spike_t *R, uint16_t *fb, int stride, float cx, float cy, float r, v3 c, float a) {
    if (a < 0.01f || r < 0.5f) return;
    int x0 = (int)(cx - r), x1 = (int)(cx + r) + 1, y0 = (int)(cy - r), y1 = (int)(cy + r) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > R->w - 1) x1 = R->w - 1;
    if (y1 > R->h - 1) y1 = R->h - 1;
    int cr = (int)(c.x * 31 * 256), cg = (int)(c.y * 63 * 256), cb = (int)(c.z * 31 * 256);
    float ir2 = 1.0f / (r * r);
    int A8 = (int)(a * 256);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy, dy2 = dy * dy;
        if (dy2 >= r * r) continue;
        float hw = fsqrt(r * r - dy2);
        int xs = (int)(cx - hw), xe = (int)(cx + hw);
        if (xs < x0) xs = x0;
        if (xe > x1) xe = x1;
        uint16_t *row = fb + y * stride;
        for (int x = xs; x <= xe; x++) {
            float dx = x + 0.5f - cx, q = 1 - (dx * dx + dy2) * ir2;
            if (q <= 0) continue;
            int w8 = (int)(q * q * A8);                      /* 0..256 */
            uint16_t d = FBX(row[x]);
            int dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
            dr += ((cr >> 8) - dr) * w8 >> 8; dg += ((cg >> 8) - dg) * w8 >> 8; db += ((cb >> 8) - db) * w8 >> 8;
            row[x] = FBX((dr << 11) | (dg << 5) | db);
        }
    }
}
/* THE CLOUD: each puff adds to a coarse lattice (a few dozen points), and each covered pixel is blended ONCE per layer
   from its cell's four corners - not once per overlapping puff (the first board run: 84 puffs cost 13-18 ms, 19 fps) */
static void stamp_puff(piranha_spike_t *R, const puff_t *q, int layer) {
    float k = kz(q->p.z), sx, sy; proj(R, q->p, k, &sx, &sy);
    float g = 1 - fexpn(q->age / 2.6f);
    float r = (q->r0 + (q->r1 - q->r0) * g) * k / CG;
    float life = q->age / q->life, a = q->a0 * clampf(q->age / 0.25f, 0, 1) * (1 - life) * (1 - life * 0.5f) * (1 - 0.5f * fogf(q->p.z));
    if (a < 0.005f || r < 0.2f) return;
    float cx = sx / CG, cy = sy / CG;
    int x0 = (int)(cx - r), x1 = (int)(cx + r) + 1, y0 = (int)(cy - r), y1 = (int)(cy + r) + 1;
    int gw = R->w / CG + 1, gh = R->h / CG + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > gw) x1 = gw;
    if (y1 > gh) y1 = gh;
    if (x0 > x1 || y0 > y1) return;
    float ir2 = 1 / (r * r);
    uint16_t *L = R->cloud[layer];
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float dx = x - cx, dy = y - cy, qq = 1 - (dx * dx + dy * dy) * ir2;
        if (qq <= 0) continue;
        int v = L[y * LW + x] + (int)(qq * qq * a * 4096);
        L[y * LW + x] = (uint16_t)(v > 65535 ? 65535 : v);
    }
    if (x0 < R->cx0[layer]) R->cx0[layer] = x0;
    if (x1 > R->cx1[layer]) R->cx1[layer] = x1;
    if (y0 < R->cy0[layer]) R->cy0[layer] = y0;
    if (y1 > R->cy1[layer]) R->cy1[layer] = y1;
    R->cfog[layer] += fogf(q->p.z); R->cn[layer]++;
}
static void composite_cloud(piranha_spike_t *R, uint16_t *fb, int stride, int layer) {
    if (!R->cn[layer]) return;
    const uint16_t *L = R->cloud[layer];
    float fog = R->cfog[layer] / R->cn[layer] * 0.7f;
    static uint8_t A8[256]; static int a8_ok;                       /* density (x64) -> alpha x256: 1 - e^-d, held under 0.9 */
    if (!a8_ok) { for (int i = 0; i < 256; i++) { float d = i / 64.0f; float a = (1 - expf(-d * 1.25f)) * 0.9f; A8[i] = (uint8_t)(a * 255); } a8_ok = 1; }
    v3 blood = V(0.50f, 0.03f, 0.025f);
    for (int cy = R->cy0[layer]; cy < R->cy1[layer]; cy++) {
        v3 c = mix3(blood, murk(R, cy * CG + 2), fog);
        int tr = (int)(clampf(tone(c.x), 0, 1) * 31), tg = (int)(clampf(tone(c.y), 0, 1) * 63), tb = (int)(clampf(tone(c.z), 0, 1) * 31);
        for (int cx = R->cx0[layer]; cx < R->cx1[layer]; cx++) {
            int a = L[cy * LW + cx], b = L[cy * LW + cx + 1], c0 = L[(cy + 1) * LW + cx], d = L[(cy + 1) * LW + cx + 1];
            if ((a | b | c0 | d) < 24) continue;
            int px0 = cx * CG, py0 = cy * CG;
            for (int j = 0; j < CG; j++) {
                int y = py0 + j; if (y >= R->h) break;
                int l = a + (c0 - a) * j / CG, r = b + (d - b) * j / CG;          /* x4096 */
                uint16_t *row = fb + y * stride;
                for (int i = 0; i < CG; i++) {
                    int x = px0 + i; if (x >= R->w) break;
                    int dd = (l + (r - l) * i / CG) >> 6;                       /* x64 */
                    int w8 = A8[dd > 255 ? 255 : dd];
                    if (!w8) continue;
                    uint16_t p = FBX(row[x]);
                    int pr = (p >> 11) & 31, pg = (p >> 5) & 63, pb = p & 31;
                    pr += (tr - pr) * w8 >> 8; pg += (tg - pg) * w8 >> 8; pb += (tb - pb) * w8 >> 8;
                    row[x] = FBX((pr << 11) | (pg << 5) | pb);
                }
            }
        }
    }
}
static void draw_meat(piranha_spike_t *R, uint16_t *fb, int stride, const meat_t *m, int *px) {
    float k = kz(m->p.z), sx, sy; proj(R, m->p, k, &sx, &sy);
    float r = meat_r(m) * k, rr = r * 1.25f;
    int x0 = (int)(sx - rr), x1 = (int)(sx + rr) + 1, y0 = (int)(sy - rr), y1 = (int)(sy + rr) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > R->w - 1) x1 = R->w - 1;
    if (y1 > R->h - 1) y1 = R->h - 1;
    float ca = fcos(-m->ang), sa = fsin(-m->ang), ir = 1 / r;
    float fog = m->wet ? fogf(m->p.z) : 0, att = m->wet ? fexpn((m->p.y - R->SURF) * 0.003f) : 1;
    v3 mc = murk(R, m->p.y);
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = fb + y * stride;
        for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f - sx) * ir, dy = (y + 0.5f - sy) * ir;
            float lx = dx * ca - dy * sa, ly = dx * sa + dy * ca;
            float d2 = lx * lx + ly * ly;
            if (d2 > 1.6f) continue;
            float th = atan2f(ly, lx);
            float rad = 1 + 0.14f * fsin(th * 3 + m->seed) + 0.08f * fsin(th * 5 + m->seed * 2.1f);
            if (d2 > rad * rad) continue;
            int hole = 0; float hedge = 9;
            for (int h = 0; h < m->nh; h++) { float hx = lx - m->hx[h], hy = ly - m->hy[h], q = hx * hx + hy * hy, hr2 = m->hr[h] * m->hr[h];
                if (q < hr2) { hole = 1; break; } float e = q / hr2; if (e < hedge) hedge = e; }
            if (hole) continue;
            float edge = rad - fsqrt(d2);
            v3 c = V(0.52f, 0.07f, 0.07f);
            float marb = fsin(lx * 7 + ly * 2.5f + m->seed) * fsin(ly * 4 - lx * 1.5f + m->seed * 1.7f);
            if (marb > 0.72f) c = V(0.86f, 0.66f, 0.62f);
            if (lx * 0.6f + ly * 0.8f > 0.62f * rad) c = V(0.90f, 0.82f, 0.70f);       /* the fat cap */
            if (hedge < 1.5f) c = V(0.74f, 0.11f, 0.10f);                                 /* raw where it was bitten */
            float sh = (0.55f + 0.5f * (-dy)) * att;
            if (edge < 0.12f) sh *= 0.55f;
            c = mul(c, sh);
            c = mix3(c, mc, fog);
            row[x] = FBX(pack565(tone(c.x), tone(c.y), tone(c.z)));
            (*px)++;
        }
    }
}
static void put_px(const piranha_spike_t *R, uint16_t *fb, int stride, int x, int y, v3 c, float a) {
    if (x < 0 || y < 0 || x >= R->w || y >= R->h) return;
    v3 d = unpack565(FBX(fb[y * stride + x]));
    fb[y * stride + x] = FBX(pack565(d.x + (c.x - d.x) * a, d.y + (c.y - d.y) * a, d.z + (c.z - d.z) * a));
}
static void draw_bit(piranha_spike_t *R, uint16_t *fb, int stride, const bit_t *b) {
    float k = kz(b->p.z), sx, sy; proj(R, b->p, k, &sx, &sy);
    float fog = fogf(b->p.z); v3 c = mix3(V(0.62f, 0.10f, 0.09f), murk(R, b->p.y), fog);
    int r = b->r * k > 1.4f ? 1 : 0;
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) put_px(R, fb, stride, (int)sx + x, (int)sy + y, c, (x || y) ? 0.6f : 1);
}
static void draw_overlays(piranha_spike_t *R, uint16_t *fb, int stride) {
    /* bubbles rising from the splash */
    for (int i = 0; i < R->nbub; i++) {
        const bub_t *b = &R->bub[i];
        float k = kz(b->p.z), sx, sy; proj(R, b->p, k, &sx, &sy);
        v3 c = V(0.70f, 0.66f, 0.48f);
        put_px(R, fb, stride, (int)sx, (int)sy, c, 0.7f);
        if (b->r > 1.3f) { put_px(R, fb, stride, (int)sx + 1, (int)sy, c, 0.35f); put_px(R, fb, stride, (int)sx, (int)sy - 1, c, 0.35f); }
    }
    /* the rings on the surface, seen from below */
    for (int i = 0; i < R->nring; i++) {
        const ring_t *g = &R->ring[i];
        float age = R->T - g->t0; if (age > 1.6f) continue;
        float k = kz(g->z), sx, sy; proj(R, V(g->x, R->SURF, g->z), k, &sx, &sy);
        float rr = (6 + 38 * age) * k, ry = rr * 0.16f, a = 0.45f * (1 - age / 1.6f);
        for (int s = 0; s < 48; s++) { float an = s * (2 * PI / 48); put_px(R, fb, stride, (int)(sx + rr * fcos(an)), (int)(sy + 2 + ry * fsin(an)), V(0.75f, 0.70f, 0.45f), a); }
    }
}
static void draw_motes(piranha_spike_t *R, uint16_t *fb, int stride, int nearer) {
    for (int i = 0; i < NMOTE; i++) {
        const v3 p = R->mote[i];
        if ((p.z < 60) != nearer) continue;
        float k = kz(p.z), sx, sy; proj(R, p, k, &sx, &sy);
        float a = 0.30f * (1 - fogf(p.z)) * (0.6f + 0.4f * fsin(R->T * 0.8f + i));
        put_px(R, fb, stride, (int)sx, (int)sy, V(0.55f, 0.50f, 0.32f), a);
    }
}
static void front_roots(piranha_spike_t *R, uint16_t *fb, int stride) {
    for (int i = 0; i < R->nrun; i++) {
        const run_t *q = &R->run[i];
        int y = q->off / R->w, x = q->off % R->w;
        uint16_t *dst = fb + y * stride + x;
        const uint16_t *src = R->ovc + q->src; const uint8_t *al = R->ova + q->src;
        for (int k = 0; k < q->len; k++) {
            if (al[k] == 255) dst[k] = src[k];
            else {
                uint16_t s = FBX(src[k]), d = FBX(dst[k]);
                int a = al[k];
                int r5 = (((s >> 11) & 31) * a + ((d >> 11) & 31) * (255 - a)) >> 8, g6 = (((s >> 5) & 63) * a + ((d >> 5) & 63) * (255 - a)) >> 8, b5 = ((s & 31) * a + (d & 31) * (255 - a)) >> 8;
                dst[k] = FBX((r5 << 11) | (g6 << 5) | b5);
            }
        }
    }
}

#ifndef ESP_PLATFORM   /* the sim's window on the clock (sim/selftest_xtclock.c): 0 T (set first when set >= 0), 1 the newest ring's age, 2 drops asked for */
float piranha_spike_test(piranha_spike_t *R, int what, float set) {
    if (what == 0) { if (set >= 0) R->T = set; return R->T; }
    return what == 1 ? (R->nring ? R->T - R->ring[R->nring - 1].t0 : -1) : (float)R->feed_req;
}
#endif
/* ------------------------------------------------------------------ the frame */
void piranha_spike_frame(piranha_spike_t *R, uint16_t *fb, int stride, double clock, bool primed) {
    lut_init();
    if (!primed && R->scene) for (int y = 0; y < R->h; y++) memcpy(fb + y * stride, R->scene + y * R->w, sizeof(uint16_t) * R->w);
    float dt;
    if (!R->started) { R->started = 1; dt = 1.0f / 25; R->clock0 = clock; }
    else { dt = (float)(clock - R->clock0); R->clock0 = clock; }
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    int64_t t0 = now_us();
    R->T += dt;
    if (R->T >= T_LAP) { R->T -= T_LAP; R->tt0 -= T_LAP; for (int i = 0; i < R->nring; i++) R->ring[i].t0 -= T_LAP; }   /* the tap and the rings keep their ages */
    for (int i = 0; i < R->feed_req; i++) drop_meat(R, R->feed_x[i]);
    R->feed_req = 0;
    if (R->auto_s > 0) { R->auto_t -= dt; if (R->auto_t <= 0) { R->auto_t = R->auto_s; if (!food_live(R)) drop_meat(R, R->w * (0.25f + 0.5f * frand(R))); } }
    food_tick(R, dt);
    {   int food = food_live(R);
        if (food) R->frenzy += (1 - R->frenzy) * clampf(dt * 3, 0, 1);
        else R->frenzy -= dt * 0.12f;
        if (R->frenzy < 0) R->frenzy = 0; }
    int busy = 0;
    for (int i = 0; i < R->nf; i++) { fish_tick(R, &R->fish[i], i, dt); busy += R->fish[i].st != S_CALM; }
    R->st.fish_busy = busy; R->st.puffs = R->npuff;
    int64_t t1 = now_us();
    /* the draw list, far to near: fish, puffs, meat, bits */
    item_t *it = R->items;
    int n = 0;
    for (int i = 0; i < R->nf; i++) { it[n].kind = 0; it[n].i = (int16_t)i; it[n].z = R->fish[i].p.z; n++; }
    for (int i = 0; i < MAXMEAT; i++) if (R->meat[i].on) { it[n].kind = 2; it[n].i = (int16_t)i; it[n].z = R->meat[i].p.z; n++; }
    for (int i = 0; i < MAXBIT; i++) if (R->bit[i].on) { it[n].kind = 3; it[n].i = (int16_t)i; it[n].z = R->bit[i].p.z; n++; }
    for (int i = 1; i < n; i++) { item_t q = it[i]; int j = i - 1; while (j >= 0 && it[j].z < q.z) { it[j + 1] = it[j]; j--; } it[j + 1] = q; }
    draw_motes(R, fb, stride, 0);
    int px = 0, tris = 0; int64_t vus = 0, rus = 0, fus = 0;
    {   int64_t a = now_us();
        float zs = 0; for (int i = 0; i < R->nf; i++) zs += R->fish[i].p.z;
        zs /= R->nf;                                                    /* the shoal's middle splits the cloud */
        for (int l = 0; l < 2; l++) {
            if (R->cn[l]) for (int y = R->cy0[l]; y <= R->cy1[l]; y++) memset(&R->cloud[l][y * LW + R->cx0[l]], 0, sizeof(uint16_t) * (R->cx1[l] - R->cx0[l] + 1));
            R->cn[l] = 0; R->cfog[l] = 0; R->cx0[l] = R->cy0[l] = 1 << 20; R->cx1[l] = R->cy1[l] = -1;
        }
        for (int i = 0; i < R->npuff; i++) stamp_puff(R, &R->puff[i], R->puff[i].p.z < zs);
        composite_cloud(R, fb, stride, 0);
        fus += now_us() - a; }
    for (int i = 0; i < n; i++) {
        int64_t a = now_us();
        if (it[i].kind == 0) {
            const fish_t *F = &R->fish[it[i].i];
            mesh_fish(R, F);
            int64_t b = now_us(); vus += b - a;
            draw_mesh(R, fb, stride, F->p.z, F->S * 0.7f, &px);
            tris += R->ntris;
            rus += now_us() - b;
        } else {
            if (it[i].kind == 2) draw_meat(R, fb, stride, &R->meat[it[i].i], &px);
            else draw_bit(R, fb, stride, &R->bit[it[i].i]);
            fus += now_us() - a;
        }
    }
    int64_t t2 = now_us();
    composite_cloud(R, fb, stride, 1);
    draw_motes(R, fb, stride, 1);
    draw_overlays(R, fb, stride);
    front_roots(R, fb, stride);
    int64_t t3 = now_us();
    R->st.verts_ms = (t1 - t0 + vus) / 1000.0f; R->st.raster_ms = rus / 1000.0f; R->st.fx_ms = (fus + t3 - t2) / 1000.0f;
    R->st.tris_drawn = tris; R->st.px_drawn = px;
}
