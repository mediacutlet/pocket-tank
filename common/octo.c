/* The octopus (look A, the ring of eight) as the firmware would draw it: a tone grid and a part grid built at cell
 * centres, then laid on the glass through a palette. A port of working-assets/octopus (octo_proto.py build + paint,
 * octo_motion.py Arm2.bezier) written the way render.c builds the crab: no bitmaps, no per-cell library calls
 * (the S3 has no float divide and pow/sqrt/sin are calls: they are tables or once-a-build here).
 * PROTOTYPE for measuring only (docs/octopus.md, "What it costs"). Not part of the firmware. */
#include <math.h>
#include <string.h>
#include "octo.h"
#ifdef OCTO_PROF            /* the bench's stopwatch: where a build's time goes (tools/s3octo, DEFS=OCTO_PROF) */
#include "esp_timer.h"
int64_t octo_prof[8];
#define PT0() int64_t pt_ = esp_timer_get_time()
#define PT(i) do { const int64_t n_ = esp_timer_get_time(); octo_prof[i] += n_ - pt_; pt_ = n_; } while (0)
#else
#define PT0()
#define PT(i)
#endif
#define IFLOOR(v) ((int)((v) + 4096.0f) - 4096)     /* floor without the library call (the S3 calls out for floorf and ceilf) */

#define OCTO_BEZ_IT 7
#define T_EYE 6
#define T_PUP 7
#define T_GLINT 8
#define P_SACK 20
#define P_HEAD 21
#define P_BUMP 22
#define P_EYE 30

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define OCTO_TAB EXT_RAM_BSS_ATTR      /* (on a board these three tables, 1.4 KB, are in PSRAM: internal RAM has about 23 KB free) */
#else
#define OCTO_TAB
#endif
static OCTO_TAB float g_pow[65];          /* |t|^2.2 for t = 0..1 (the sack's soft egg) */
static OCTO_TAB float g_bulge[33];        /* 1 / (0.74 + 0.26 sin(clip(1.25 t) pi/2)): one over the egg's swell (the S3 has no float divide: a divide is a call) */
static OCTO_TAB float g_sq[4 * 64 + 2];   /* sqrt(i / 4): a disc's half width on a row, with no call (the S3 has no hardware square root) */
static int g_tabs;
static void tabs(void) {
    if (g_tabs) return;
    for (int i = 0; i < 4 * 64 + 2; i++) g_sq[i] = sqrtf(i / 4.0f);
    for (int i = 0; i <= 64; i++) g_pow[i] = powf(i / 64.0f, 2.2f);
    for (int i = 0; i <= 32; i++) { float t = i / 32.0f * 1.25f; if (t > 1) t = 1; g_bulge[i] = 1.0f / (0.74f + 0.26f * sinf(t * 1.5707963f)); }
    g_tabs = 1;
}
void octo_look_init(octo_look_t *lk) {            /* once a size: an arm's half width along it */
    tabs();
    for (int i = 0; i <= OCTO_WT; i++) lk->wtab[i] = lk->w1 + (lk->w0 - lk->w1) * powf(1.0f - (float)i / OCTO_WT, lk->taper);
}
static inline float arm_w(const octo_look_t *lk, float t, int far) {
    const float w = lk->wtab[(int)(t * OCTO_WT + 0.5f)];
    return far ? w * 0.86f : w;
}
static inline float pow22(float a) {              /* a in 0..1 */
    const float f = a * 64.0f; const int i = (int)f;
    return g_pow[i] + (g_pow[i + 1] - g_pow[i]) * (f - (float)i);
}
static inline uint32_t hash2(int i, int j) {
    uint32_t h = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u + 11u * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) & 0xFFFF;
}

/* ---- the planted pose of one arm (Arm2.bezier): out of the body along a0, over, and down flat onto the sand ---- */
void octo_bezier(const octo_look_t *lk, float *P, int N, float rx, float ry, float ca, float sa, float tx, float ty, float L0, float Lmax, float floor, int far) {
    const float dx = tx - rx, dy = ty - ry, chord = sqrtf(dx * dx + dy * dy);
    float want = chord * 1.2f + 1.5f; if (want > Lmax) want = Lmax;
    if (want < L0 * 0.7f) want = L0 * 0.7f;
    if (want < chord * 1.01f) want = chord * 1.01f;
    const float sg = dx >= 0 ? 1.0f : -1.0f, adx = fabsf(dx) * 0.9f + 1.0f;
    float b[OCTO_NMAX][4];
    const float inv = 1.0f / (float)(N - 1);
    for (int i = 0; i < N; i++) { const float u = i * inv, v = 1 - u; b[i][0] = v * v * v; b[i][1] = 3 * v * v * u; b[i][2] = 3 * v * u * u; b[i][3] = u * u * u; }
    float lo = 0.15f, hi = 2.0f, k = lo;
    for (int it = 0; it < OCTO_BEZ_IT; it++) {    /* the first pass is k = lo; then halvings (six: k to 3 parts in 100, under a pixel of arm) */
        const float m1 = k * want * 0.45f; float m2 = k * want * 0.40f; if (m2 > adx) m2 = adx;
        const float c1x = rx + ca * m1, c1y = ry + sa * m1, c2x = tx - sg * m2;
        float len = 0, px = 0, py = 0;
        for (int i = 0; i < N; i++) {
            const float x = b[i][0] * rx + b[i][1] * c1x + b[i][2] * c2x + b[i][3] * tx;
            const float y = b[i][0] * ry + b[i][1] * c1y + b[i][2] * ty + b[i][3] * ty;
            if (i) { const float ex = x - px, ey = y - py; len += sqrtf(ex * ex + ey * ey); }
            px = x; py = y; P[2 * i] = x; P[2 * i + 1] = y;
        }
        if (it == 0) { if (len >= want) break; }
        else if (len < want) lo = k; else hi = k;
        k = (lo + hi) * 0.5f;
        if (it == OCTO_BEZ_IT - 1) {              /* lay it at lo */
            const float n1 = lo * want * 0.45f; float n2 = lo * want * 0.40f; if (n2 > adx) n2 = adx;
            const float d1x = rx + ca * n1, d1y = ry + sa * n1, d2x = tx - sg * n2;
            for (int i = 0; i < N; i++) { P[2 * i] = b[i][0] * rx + b[i][1] * d1x + b[i][2] * d2x + b[i][3] * tx; P[2 * i + 1] = b[i][0] * ry + b[i][1] * d1y + b[i][2] * ty + b[i][3] * ty; }
        }
    }
    for (int i = 1; i < N; i++) { const float fl = floor - arm_w(lk, i * inv, far); if (P[2 * i + 1] > fl) P[2 * i + 1] = fl; }
}

/* ---- the grid ---- */
typedef struct { int y0, y1; short lo[OCTO_GH], hi[OCTO_GH]; } box_t;   /* an arm's cells: its rows, and on each row from where to where */
/* a disc, row by row: the row's span is worked out once (a table, no call) and filled, never a test a cell (0.4.3 cost pass:
   the first cut asked every cell of the disc's box, and the arms were ~3/4 of the whole build) */
static inline void disc(octo_grid_t *g, float x, float y, float w, int tone, int pid, box_t *bb) {
    int gy0 = IFLOOR(y - w - g->y0), gy1 = IFLOOR(y + w - g->y0) + 1;
    if (gy0 < 0) { gy0 = 0; } if (gy1 > g->hf) { gy1 = g->hf; }                              /* hf: rows above the sand */
    if (gy0 >= gy1) return;
    const float w2 = w * w, fy = g->y0 + 0.5f - y, cx = x - g->x0 - 0.5f;
    for (int gy = gy0; gy < gy1; gy++) {
        const float dy = gy + fy, r = w2 - dy * dy;
        if (r < 0) continue;
        const float hw = r < 64.0f ? g_sq[(int)(r * 4.0f)] : sqrtf(r);
        int x0 = IFLOOR(cx - hw) + 1, x1 = IFLOOR(cx + hw) + 1;
        if (x0 < 0) { x0 = 0; } if (x1 > g->w) { x1 = g->w; }
        if (x0 >= x1) continue;
        uint8_t *tn = g->tone + gy * OCTO_GW, *pt = g->part + gy * OCTO_GW;
        for (int gx = x0; gx < x1; gx++) { tn[gx] = (uint8_t)tone; pt[gx] = (uint8_t)pid; }
        if (x0 < bb->lo[gy]) { bb->lo[gy] = (short)x0; } if (x1 > bb->hi[gy]) { bb->hi[gy] = (short)x1; }
    }
    if (gy0 < bb->y0) { bb->y0 = gy0; } if (gy1 > bb->y1) { bb->y1 = gy1; }
}
static void stamp_arm(octo_grid_t *g, const octo_look_t *lk, const octo_arm_t *a, int pid, float thin) {
    const float *P = a->P; const int N = lk->N;
    box_t bb; bb.y0 = OCTO_GH; bb.y1 = 0;
    for (int i = 0; i < g->hf; i++) { bb.lo[i] = OCTO_GW; bb.hi[i] = 0; }
    const int tone = a->far ? 2 : 3;
    const float invN = 1.0f / (float)(N - 1);
    for (int i = 0; i < N - 1; i++) {             /* Catmull-Rom through the chain. A stamp every 0.8 of the arm's half width there (a pixel at least):
                                                     thick near the body the discs overlap ten deep at a stamp a pixel, and the outline is the same to under half a pixel */
        float wi = arm_w(lk, (i + 0.5f) * invN, a->far) * thin * 0.80f; if (wi < 1.0f) wi = 1.0f;
        int k = (int)(a->seg * OCTO_STAMPS / wi) + 1;             /* (one at least: where the arm is thick a stamp a segment is enough) */
        const float invk = 1.0f / (float)k;
        const float *p0 = P + 2 * (i > 0 ? i - 1 : 0), *p1 = P + 2 * i, *p2 = P + 2 * (i + 1), *p3 = P + 2 * (i + 2 < N ? i + 2 : N - 1);
        const float ax = -p0[0] + p2[0], bx = 2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0], cx = -p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0];
        const float ay = -p0[1] + p2[1], by = 2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1], cy = -p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1];
        for (int j = 0; j < k; j++) {
            const float u = j * invk;
            const float x = 0.5f * (2 * p1[0] + (ax + (bx + cx * u) * u) * u), y = 0.5f * (2 * p1[1] + (ay + (by + cy * u) * u) * u);
            float w = arm_w(lk, (i + u) * invN, a->far) * thin; if (w < 0.62f) w = 0.62f;
            disc(g, x, y, w, tone, pid, &bb);
        }
    }
    float w = arm_w(lk, 1.0f, a->far) * thin; if (w < 0.62f) w = 0.62f;
    float x = P[2 * (N - 1)], y = P[2 * (N - 1) + 1];
    disc(g, x, y, w, tone, pid, &bb);
    const float c = a->curl * lk->curl;           /* the hook: the last of the arm curls up off the sand */
    if (c > 0.05f) {
        float dx = x - P[2 * (N - 2)], dy = y - P[2 * (N - 2) + 1];
        const float il = 1.0f / sqrtf(dx * dx + dy * dy + 1e-6f); dx *= il; dy *= il;
        const float sgn = dx >= 0 ? -1.0f : 1.0f, an = sgn * 0.5934f * c, cr = cosf(an), sr = sinf(an), hs = 0.75f * lk->sz;
        const int n = (int)(7 * c);
        for (int j = 0; j < n; j++) {
            const float nx = dx * cr - dy * sr, ny = dx * sr + dy * cr; dx = nx; dy = ny;
            x += dx * hs; y += dy * hs;
            disc(g, x, y, w, tone, pid, &bb);
        }
    }
    /* its light and its shade: the top row of it a tone up, the bottom row a tone down */
    for (int gy = bb.y0; gy < bb.y1; gy++) {
        uint8_t *tn = g->tone + gy * OCTO_GW; const uint8_t *pt = g->part + gy * OCTO_GW;
        for (int gx = bb.lo[gy]; gx < bb.hi[gy]; gx++) {
            if (pt[gx] != pid) continue;
            const int up = gy > 0 && pt[gx - OCTO_GW] == pid, dn = gy + 1 < g->h && pt[gx + OCTO_GW] == pid;
            if (!up && dn) tn[gx]++; else if (up && !dn) tn[gx]--;
        }
    }
}
static void eye(octo_grid_t *g, const octo_look_t *lk, const octo_pose_t *p, float ox, float oy, int far, float cr, float sr) {
    const float ex = floorf(p->x + ox * cr - oy * sr + 0.5f), ey = floorf(p->y + ox * sr + oy * cr + 0.5f), r = lk->eye_r;
    const float rb = (r + 0.9f) * (r + 0.9f), r2 = r * r, rp = (r - 1.0f) * (r - 1.0f);
    int gx0 = (int)(ex - r - 2 - g->x0), gx1 = (int)(ex + r + 3 - g->x0), gy0 = (int)(ey - r - 2 - g->y0), gy1 = (int)(ey + r + 3 - g->y0);
    if (gx0 < 0) { gx0 = 0; } if (gy0 < 0) { gy0 = 0; } if (gx1 > g->w) { gx1 = g->w; } if (gy1 > g->hf) { gy1 = g->hf; }
    for (int gy = gy0; gy < gy1; gy++)
        for (int gx = gx0; gx < gx1; gx++) {
            const float dx = g->x0 + gx + 0.5f - ex, dy = g->y0 + gy + 0.5f - ey, d2 = dx * dx + dy * dy;
            if (d2 > rb) continue;
            uint8_t *tn = g->tone + gy * OCTO_GW + gx, *pt = g->part + gy * OCTO_GW + gx;
            if (*pt != P_EYE) { *tn = far ? 3 : 4; *pt = P_BUMP; }
            if (d2 <= r2) {
                *tn = T_EYE; *pt = P_EYE;
                if (d2 <= rp) *tn = T_PUP;
                if (fabsf(dx + 0.9f) < 0.6f && fabsf(dy + 0.9f) < 0.6f) *tn = T_GLINT;
                if (p->blink > 0.5f) { *tn = fabsf(dy) < 0.6f ? 2 : 3; *pt = P_BUMP; }
            }
        }
}
/* the sack: a soft egg along its axis. Its own function, not inlined: a hot loop inside the one big build spills its registers on the S3 */
static __attribute__((noinline)) void sack(octo_grid_t *g, const octo_look_t *lk, const octo_pose_t *p, float ax, float ay, float breath, int box[4]) {
        const float nx = -ay, ny = ax;
        const float mlen = lk->m_len * (1 + 0.16f * p->squeeze - 0.05f * p->puff);
        const float mb = lk->m_b * (1 - 0.20f * p->squeeze + 0.10f * p->puff + 0.035f * breath);
        const float u0 = -lk->head[0] * 0.35f, uc = u0 + mlen * 0.5f, imlen = 1.0f / mlen, ih = 2.0f * imlen, imb = 1.0f / mb;
        const float droop = lk->m_droop * p->droop * ny, iq = 1.0f / 2.6f;
        const float ex = p->x + ax * uc, ey = p->y + ay * uc, ha = mlen * 0.5f, hb = mb + fabsf(droop);   /* its box: the egg's own, turned (the first cut took a square about its middle: 4x the cells) */
        const float rx = fabsf(ha * ax) + fabsf(hb * nx) + 2, ry = fabsf(ha * ay) + fabsf(hb * ny) + 2;
        int gx0 = (int)(ex - rx - g->x0), gx1 = (int)(ex + rx - g->x0) + 1, gy0 = (int)(ey - ry - g->y0), gy1 = (int)(ey + ry - g->y0) + 1;
        box[0] = gx0; box[1] = gy0; box[2] = gx1; box[3] = gy1;                         /* (its box as it would be, before the grid's edges cut it: the sack's replay) */
        if (gx0 < 0) { gx0 = 0; } if (gy0 < 0) { gy0 = 0; } if (gx1 > g->w) { gx1 = g->w; } if (gy1 > g->hf) { gy1 = g->hf; }
        if (box[1] < 0 || box[3] > g->hf || box[0] < 0 || box[2] > g->w) box[0] = -1;   /* (cut by the grid or the sand: not kept) */
        const int mot = (int)(lk->mottle * 65535.0f);
        const float l0 = -(ax * 0.45f + ay * 0.9f), l1 = -(nx * 0.45f + ny * 0.9f);      /* the light, along and across it */
        const float qx0 = g->x0 + gx0 + 0.5f - p->x;
        for (int gy = gy0; gy < gy1; gy++) {
            uint8_t *tn = g->tone + gy * OCTO_GW, *pt = g->part + gy * OCTO_GW;
            const float qy = g->y0 + gy + 0.5f - p->y;
            float u = qx0 * ax + qy * ay - ax, v = qx0 * nx + qy * ny - nx;              /* stepped along the row: no multiply a cell */
            int hu = 1 << 30, hv = 0, hh = 0;
            for (int gx = gx0; gx < gx1; gx++) {
                u += ax; v += nx;
                const float nu = (u - uc) * ih, anu = fabsf(nu);
                if (anu > 1.0f) continue;
                float tt = (u - u0) * imlen; if (tt < 0) tt = 0; if (tt > 1) tt = 1;
                const float vd = v - droop * tt * tt;
                const float nv = vd * imb * g_bulge[(int)(tt * 32.0f + 0.5f)], anv = fabsf(nv);
                if (anv > 1.0f) continue;
                if (anu * anu + anv * anv > 1.0f && pow22(anu) + pow22(anv) > 1.0f) continue;   /* (inside the plain ellipse is inside the egg: |a|^2.2 <= a^2. Only the rim asks the table) */
                const float lit = nu * l0 + nv * l1;
                int t = lit > 0.42f ? 4 : lit > -0.30f ? 3 : 2;
                if (mot > 0 && t > 2) {
                    const int iu = IFLOOR(u * iq), iv = IFLOOR(vd * iq);
                    if (iu != hu || iv != hv) { hu = iu; hv = iv; hh = (int)hash2(iu, iv); }      /* (a freckle's cell is 2.6 px: the same answer for the pixels beside it) */
                    if (hh < mot) { const float cu = (iu + 0.5f) * 2.6f - u, cv = (iv + 0.5f) * 2.6f - vd; if (cu * cu + cv * cv < 1.11f) t--; }
                }
                tn[gx] = (uint8_t)t; pt[gx] = P_SACK;
            }
        }
    }
/* THE SACK, KEPT (0.4.3, Strato: "yes its worth it" - the walking cost). While it walks the sack mostly slides: its shape changes only with
   its pose. The sack is drawn at its pose QUANTIZED - its place to half a pixel, the breath to 9 levels, squeeze, puff and droop to 1/32
   (its turn exact) - and the cells of the last OCTO_SACKS shapes are kept, keyed by that pose, and laid down again, whole-pixel shifted,
   when the same shape comes back: the same cells a fresh raster at that pose lays (octo-den 26 holds it). A shape cut by the grid's edge
   or the sand is never kept nor laid down from the cache. On a board the cache is in PSRAM */
#define OCTO_SACKS 8
#define OCTO_SACK_CELLS 2400
typedef struct { int8_t dx, dy; uint8_t t; } sack_cell_t;
typedef struct { uint32_t key[8]; int n, used, bw, bh; sack_cell_t c[OCTO_SACK_CELLS]; } sack_entry_t;
#ifdef ESP_PLATFORM
static EXT_RAM_BSS_ATTR sack_entry_t g_sack[OCTO_SACKS];
#else
static sack_entry_t g_sack[OCTO_SACKS];
#endif
static int g_sack_clock, g_sack_off, g_sack_bypass;
void octo_debug_sack_bypass(int on) { g_sack_bypass = on; }   /* (tests: this build draws the sack fresh, the kept shapes untouched) */
long g_sack_hits, g_sack_miss;
void octo_debug_sack_cache(int on) { g_sack_off = !on; for (int i = 0; i < OCTO_SACKS; i++) g_sack[i].n = 0; }
static void sack_kept(octo_grid_t *g, const octo_look_t *lk, const octo_pose_t *p, float ax, float ay) {
    octo_pose_t q = *p;                                                  /* the quantized pose: the sack is always drawn at it, kept or not */
    q.x = floorf(p->x * 2.0f + 0.5f) * 0.5f; q.y = floorf(p->y * 2.0f + 0.5f) * 0.5f;
    q.squeeze = floorf(p->squeeze * 32.0f + 0.5f) / 32.0f; q.puff = floorf(p->puff * 32.0f + 0.5f) / 32.0f; q.droop = floorf(p->droop * 32.0f + 0.5f) / 32.0f;
    const float breath = floorf(sinf(p->t * 2.4f) * 4.0f + 0.5f) / 4.0f;
    const int ox = (int)floorf(q.x), oy = (int)floorf(q.y);
    uint32_t key[8]; float kf[8] = { q.x - ox, q.y - oy, q.squeeze, q.puff, q.droop, breath, q.theta, lk->m_b + lk->m_len * 64.0f + lk->mottle * 4096.0f + lk->head[0] * 262144.0f };
    memcpy(key, kf, sizeof key);
    int box[4];
    if (!g_sack_off && !g_sack_bypass) {
        for (int i = 0; i < OCTO_SACKS; i++) {
            sack_entry_t *e = &g_sack[i];
            if (e->n <= 0 || memcmp(e->key, key, sizeof key)) continue;
            /* its box here, as sack() would compute it: kept only if uncut here too */
            const int bx0 = ox - g->x0 + e->c[0].dx * 0, by0 = oy - g->y0;
            (void)bx0; (void)by0;
            int okb = 1;
            for (int k = 0; k < 1; k++) {
                const int x0 = e->bw >> 16, y0 = e->bh >> 16, x1 = e->bw & 0xFFFF, y1 = e->bh & 0xFFFF;   /* (the box relative to (ox, oy), packed) */
                const int gx0 = ox + (int16_t)x0 - g->x0, gy0 = oy + (int16_t)y0 - g->y0, gx1 = ox + (int16_t)x1 - g->x0, gy1 = oy + (int16_t)y1 - g->y0;
                if (gx0 < 0 || gy0 < 0 || gx1 > g->w || gy1 > g->hf) okb = 0;
            }
            if (!okb) break;
            for (int k = 0; k < e->n; k++) {
                const int gx = ox + e->c[k].dx - g->x0, gy = oy + e->c[k].dy - g->y0;
                g->tone[gy * OCTO_GW + gx] = e->c[k].t; g->part[gy * OCTO_GW + gx] = P_SACK;
            }
            e->used = ++g_sack_clock; g_sack_hits++;
            return;
        }
    }
    sack(g, lk, &q, ax, ay, breath, box);
    if (g_sack_bypass) return;
    g_sack_miss++;
    if (g_sack_off || box[0] < 0) return;
    /* keep it: the oldest entry */
    int w = 0; for (int i = 1; i < OCTO_SACKS; i++) if (g_sack[i].used < g_sack[w].used) w = i;
    sack_entry_t *e = &g_sack[w]; int n = 0, ok = 1;
    for (int gy = box[1]; gy < box[3] && ok; gy++)
        for (int gx = box[0]; gx < box[2]; gx++) {
            if (g->part[gy * OCTO_GW + gx] != P_SACK) continue;
            const int dx = g->x0 + gx - ox, dy = g->y0 + gy - oy;
            if (n >= OCTO_SACK_CELLS || dx < -127 || dx > 127 || dy < -127 || dy > 127) { ok = 0; break; }
            e->c[n].dx = (int8_t)dx; e->c[n].dy = (int8_t)dy; e->c[n].t = g->tone[gy * OCTO_GW + gx]; n++;
        }
    if (!ok) { e->n = 0; return; }
    memcpy(e->key, key, sizeof key); e->n = n; e->used = ++g_sack_clock;
    e->bw = (int)(((uint32_t)(uint16_t)(g->x0 + box[0] - ox) << 16) | (uint16_t)(g->x0 + box[2] - ox));
    e->bh = (int)(((uint32_t)(uint16_t)(g->y0 + box[1] - oy) << 16) | (uint16_t)(g->y0 + box[3] - oy));
}
void octo_build(octo_grid_t *g, const octo_look_t *lk, const octo_pose_t *p) {
    PT0();
    const float ax = cosf(p->theta), ay = sinf(p->theta);
    /* the box: the arms' points, the sack's far end, the head */
    float xn = p->x - lk->m_b - 4, xm = p->x + lk->m_b + 4, yn = p->y - lk->m_b - 6, ym = p->y + 6;
    const float reach = lk->m_len * 1.25f + lk->m_b, sx = p->x + ax * reach, sy = p->y + ay * reach;
    if (sx < xn) { xn = sx; } if (sx > xm) { xm = sx; } if (sy < yn) { yn = sy; } if (sy > ym) { ym = sy; }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < lk->N; j++) { const float x = p->arm[i].P[2 * j], y = p->arm[i].P[2 * j + 1]; if (x < xn) xn = x; if (x > xm) xm = x; if (y < yn) yn = y; if (y > ym) ym = y; }
    g->x0 = (int)floorf(xn - 8); g->y0 = (int)floorf(yn - 8);
    int x1 = (int)ceilf(xm + 8), y1 = (int)ceilf(ym + 8); const int fl = (int)ceilf(p->floor) + 1; if (y1 > fl) y1 = fl;
    g->w = x1 - g->x0; g->h = y1 - g->y0;
    if (g->w > OCTO_GW) { g->w = OCTO_GW; } if (g->h > OCTO_GH) { g->h = OCTO_GH; }
    g->hf = (int)floorf(p->floor + 0.2f - 0.5f - g->y0) + 1;          /* rows whose middle is over the sand */
    if (g->hf > g->h) { g->hf = g->h; } if (g->hf < 0) { g->hf = 0; }
    for (int gy = 0; gy < g->h; gy++) { memset(g->tone + gy * OCTO_GW, 0, (size_t)g->w); memset(g->part + gy * OCTO_GW, 0, (size_t)g->w); }

    PT(0);
    int pid = 1; g->far_n = 0;
    const float thin = 1.0f - 0.34f * (p->stream < 0 ? 0 : p->stream > 1 ? 1 : p->stream);   /* stretched out to flee: thinner arms */
    for (int i = 0; i < 8; i++) if (p->arm[i].far) { stamp_arm(g, lk, &p->arm[i], pid, thin); pid++; g->far_n++; }

    PT(1);
    sack_kept(g, lk, p, ax, ay);
    PT(2);
    /* the near arms: the widest first, so the front pair lies over the others */
    {
        int ord[8], n = 0;
        for (int i = 0; i < 8; i++) if (!p->arm[i].far) ord[n++] = i;
        for (int i = 1; i < n; i++) { const int v = ord[i]; int j = i - 1; while (j >= 0 && fabsf(lk->reach[ord[j]]) < fabsf(lk->reach[v])) { ord[j + 1] = ord[j]; j--; } ord[j + 1] = v; }
        for (int i = 0; i < n; i++) { stamp_arm(g, lk, &p->arm[ord[i]], pid, thin); pid++; }
    }
    PT(3);
    /* the head */
    {
        const float hx = lk->head[0], hy = lk->head[1], ihx = 1.0f / hx, ihy = 1.0f / hy;
        int gx0 = (int)(p->x - hx - 1 - g->x0), gx1 = (int)(p->x + hx + 2 - g->x0), gy0 = (int)(p->y - hy - 1 - g->y0), gy1 = (int)(p->y + hy + 3 - g->y0);
        if (gx0 < 0) { gx0 = 0; } if (gy0 < 0) { gy0 = 0; } if (gx1 > g->w) { gx1 = g->w; } if (gy1 > g->hf) { gy1 = g->hf; }
        for (int gy = gy0; gy < gy1; gy++) {
            const float qy = g->y0 + gy + 0.5f - p->y, e1 = (qy - 0.4f) * ihy, l1 = qy * ihy * 0.9f, rem = 1.0f - e1 * e1;
            if (rem < 0) continue;
            uint8_t *tn = g->tone + gy * OCTO_GW, *pt = g->part + gy * OCTO_GW;
            for (int gx = gx0; gx < gx1; gx++) {
                const float e0 = (g->x0 + gx + 0.5f - p->x) * ihx;
                if (e0 * e0 > rem) continue;
                const float hl = -(e0 * 0.45f + l1);
                tn[gx] = hl > 0.45f ? 4 : hl > -0.35f ? 3 : 2; pt[gx] = P_HEAD;
            }
        }
    }
    PT(4);
    {
        const float rot = p->theta - p->rest_theta, cr = cosf(rot), sr = sinf(rot);
        const float *ea = p->eye_set ? p->eye_at : lk->eye_at, *ef = p->eye_set ? p->far_eye : lk->far_eye;
        eye(g, lk, p, ef[0], ef[1], 1, cr, sr);
        eye(g, lk, p, ea[0], ea[1], 0, cr, sr);
    }
    PT(5);
    /* edges: where a part meets another below it, its last row goes one tone down (the crab's cr_dark_of) */
    for (int gy = 0; gy + 1 < g->h; gy++) {
        uint8_t *tn = g->tone + gy * OCTO_GW; const uint8_t *pt = g->part + gy * OCTO_GW;
        for (int gx = 0; gx < g->w; gx++) {
            if (!(gx & 3) && gx + 4 <= g->w && !((const uint32_t *)(const void *)pt)[gx >> 2]) { gx += 3; continue; }   /* (most of the grid is water: four cells at a look. The rows are word aligned: OCTO_GW is a multiple of 4) */
            const int a = pt[gx];
            if (!a || a >= P_EYE) continue;
            const int b = pt[gx + OCTO_GW];
            if (b && b != a && tn[gx] > 1 && tn[gx] < 5) tn[gx]--;
        }
    }
    PT(6);
}

/* ---- on the glass ---- */
void octo_palette(uint16_t pal[2][9], const uint8_t base[3], float haze) {
    static const float water[3] = { 10, 44, 58 };
    float c[9][3];
    for (int k = 0; k < 3; k++) {
        const float b = base[k];
        c[0][k] = 0; c[1][k] = b * 0.42f; c[2][k] = b * 0.66f; c[3][k] = b; c[4][k] = b + (255 - b) * 0.30f; c[5][k] = b + (255 - b) * 0.62f;
    }
    c[6][0] = 236; c[6][1] = 214; c[6][2] = 150; c[7][0] = 16; c[7][1] = 12; c[7][2] = 18; c[8][0] = c[8][1] = c[8][2] = 255;
    for (int f = 0; f < 2; f++)
        for (int i = 0; i < 9; i++) {
            int rgb[3];
            for (int k = 0; k < 3; k++) rgb[k] = (int)(f ? c[i][k] * (1 - haze) + water[k] * haze : c[i][k]);
            pal[f][i] = (uint16_t)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
        }
}
void octo_blit(uint16_t *fb, int stride, int W, int H, const octo_grid_t *g, const uint16_t pal[2][9]) {
    int gx0 = 0, gx1 = g->w, gy0 = 0, gy1 = g->h;
    if (g->x0 < 0) { gx0 = -g->x0; } if (g->y0 < 0) { gy0 = -g->y0; } if (g->x0 + gx1 > W) { gx1 = W - g->x0; } if (g->y0 + gy1 > H) { gy1 = H - g->y0; }
    for (int gy = gy0; gy < gy1; gy++) {
        const uint8_t *tn = g->tone + gy * OCTO_GW, *pt = g->part + gy * OCTO_GW;
        uint16_t *d = fb + (g->y0 + gy) * stride + g->x0;
        for (int gx = gx0; gx < gx1; gx++) {
            if (!(gx & 3) && gx + 4 <= gx1 && !((const uint32_t *)(const void *)tn)[gx >> 2]) { gx += 3; continue; }   /* (water: four cells at a look) */
            const int t = tn[gx]; if (t) d[gx] = pal[pt[gx] <= g->far_n][t];
        }
    }
}
