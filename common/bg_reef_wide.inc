/* bg_reef_wide.inc - REEF WALLS on the big screens (0.4.3, THE WIDE FRAME; included by backgrounds.c under TANK_P4;
 * docs/backgrounds-wide-0.4.3.md).
 *
 * Strato 11-21, of the pocket reef stretched over the wide glass: "completely broken; the rocks fill the screen awkwardly and
 * coral is clipping and broken on the sides. Reimagine it completely for the P4 devices." What broke it: the walls' depth
 * and the far rock were shares of the glass's width, the boulders and the colonies were scaled by that width too - 2.9
 * times, on a colony grid made for 1.75 - so the colonies ran off their grid (the clipping) and the two walls met.
 *
 * Here the reef is built of FACES. A face is a run of rock from an origin out to a ragged, ledged front that looks one
 * way: a wall is one face from the glass's side; a PINNACLE (a coral head standing on the floor) is two, back to back.
 * Each face is the pocket wall's own recipe - a ledge, a near-straight front, cut back under its foot, deep and shallow
 * in turn; boulders stamped over it; moss along each ledge's top; coral on the ledges, big kinds high and small ones
 * low - in the 1.8's sizes. A cell is ONE of the canvas's pixels (the pocket reef's is two): the same stone and the same
 * coral with twice the grain. The far rock is its own shape: a wall's reach behind each wall, a crest behind the floor.
 * The layout is kept between rebuilds like the pocket reef's (the light only colors it).
 */
static int s_reef_var = 0;                        /* the composition: 0 TWO WALLS (Strato's pick of the three, 11-21) - the sim's PT_BG_REEF shows the others: 1 walls and two pinnacles, 2 one wall and three */
void bg_debug_reef_variant(int v) { s_reef_var = v; bg_keep_drop(); }

static void rf_boulder(uint8_t (*rw)[RW_CW], int gw, int gh, float cx, float cy, float r, bool bulge) {
    const float irx = 1 / (r * 1.2f), iry = 1 / r;
    for (int jj = (int)(cy - r) - 1; jj <= (int)(cy + r) + 1; jj++) for (int ii = (int)(cx - r * 1.2f) - 1; ii <= (int)(cx + r * 1.2f) + 1; ii++) {
        if ((unsigned)jj >= (unsigned)gh || (unsigned)ii >= (unsigned)gw) continue;
        const float dx = (ii + 0.5f - cx) * irx, dy = (jj + 0.5f - cy) * iry, d = dx * dx + dy * dy;
        if (d > 1) continue;
        if (!rw[jj][ii]) { if (!bulge || d > 0.8f) continue; rw[jj][ii] = RG_BODY; }
        const float lit = -(dx * 0.7f + dy * 0.9f);                           /* the light from the upper left */
        const int l = d > 0.80f && dy > 0.1f ? 3 : lit > 0.42f ? 1 : lit < -0.38f ? 2 : 0;
        rw[jj][ii] = (uint8_t)((rw[jj][ii] & 7) | l << 4);
    }
}
/* one face: rock from the origin ox out to base[j] and its ledges, rows j0..gh-1. dir +1: the rock lies toward -x of the
   front, which looks +x (a left wall, a pinnacle's right side); -1 the mirror. pk: how far its ledges stand out (1 a wall's) */
static int rf_face(int fi, int dir, float ox, int j0, float pk, int seed, int nl) {
    __typeof__(s_s->u.re) *R = &s_s->u.re;
    uint8_t (*rw)[RW_CW] = R->k.mg; const int gw = BW, gh = BB; const float S = RF_S;
    int16_t *fc = R->face[fi]; const float *base = R->base;
    for (int j = 0; j < RW_CH; j++) fc[j] = -1;
    int j = j0, k = 0;
    while (j < gh && nl < RL_MAX) {
        const uint32_t h = bd_h(k++, 400 + seed);
        int len = (int)((14 + (h & 7)) * S); if (len < 10) len = 10;
        const float p = ((k & 1) ? 4 + ((h >> 4) & 3) : 10 + ((h >> 4) & 7)) * S * pk;
        for (int jj = j; jj < j + len && jj < gh; jj++) {
            const float t = (float)(jj - j) / len;
            float prot = p * (t < 0.6f ? 1 - 0.12f * t : 0.93f - 2.6f * (t - 0.6f)) + ((bd_h(jj / 2, 450 + seed) & 3) == 0 ? 0.6f * S : 0);
            prot += (1.6f * bd_n1(jj * (0.22f / S) + k * 5.1f, 470 + seed) - 0.8f) * S;   /* a ragged face, not a ruled line */
            if (t < 0.1f) prot -= (1 - t / 0.1f) * 1.8f * S;                  /* the ledge's lip rounded over */
            if (prot < 0) prot = 0;
            int edge = (int)(ox + dir * (base[jj] + prot)); if (edge < 0) edge = 0; if (edge > gw - 1) edge = gw - 1;
            int a = (int)ox, b = edge; if (a > b) { const int q = a; a = b; b = q; }
            if (a < 0) a = 0;
            if (b > gw - 1) b = gw - 1;
            for (int i = a; i <= b; i++) rw[jj][i] = RG_BODY;
            fc[jj] = (int16_t)edge;
        }
        { const float root = ox + dir * base[j]; R->L[nl++] = (ledge_t){ j, dir < 0, root + dir * p, root }; }
        j += len;
    }
    return nl;
}
static void rf_boulders(int fi, int dir, int j0, int seed) {
    __typeof__(s_s->u.re) *R = &s_s->u.re;
    uint8_t (*rw)[RW_CW] = R->k.mg; const int gw = BW, gh = BB; const float S = RF_S;
    const int n = (int)(300.0f * (gh - j0) / gh);
    for (int k = 0; k < n; k++) {
        const uint32_t h = bd_h(k, 520 + seed);
        const int j = j0 + (int)((h & 1023) / 1023.0f * (gh - j0));
        if (j >= gh || R->face[fi][j] < 0) continue;
        const float depth = ((h >> 10) & 255) / 255.0f, r = (2.5f + ((h >> 18) & 7) * 0.6f) * S;
        rf_boulder(rw, gw, gh, R->face[fi][j] - dir * (depth * depth * 34 * S - r * 0.35f), (float)j, r, depth < 0.15f);
    }
}
static void reef_begin(void) {
    if (s_keep && s_keep_bg == BG_REEF) { s_s->reef = s_keep; s_s->ymin = 0; return; }   /* laid out already: the light changes only its colors */
    __typeof__(s_s->u.re) *R = &s_s->u.re;
    uint8_t (*rw)[RW_CW] = R->k.mg;
    rg = R->rg; memset(R->rg, 0, sizeof R->rg); memset(R->ct, 0, sizeof R->ct); memset(&R->k, 0, sizeof R->k);
    const int gw = BW, gh = BB; const float S = RF_S;
    /* the composition: which walls, and the pinnacles (cx a share of the canvas, top a share of the water down from the surface,
       half-widths at the top and the floor in shares of BXU) */
    static const struct { uint8_t wall_l, wall_r, np; struct { float cx, top, wt, wb; } pin[3]; } VAR[3] = {
        { 1, 1, 0, { { 0 } } },
        { 1, 1, 2, { { 0.385f, 0.50f, 0.030f, 0.085f }, { 0.640f, 0.66f, 0.026f, 0.070f } } },
        { 1, 0, 3, { { 0.400f, 0.36f, 0.036f, 0.100f }, { 0.655f, 0.56f, 0.030f, 0.085f }, { 0.895f, 0.70f, 0.026f, 0.080f } } },
    };
    const int var = (unsigned)s_reef_var < 3 ? s_reef_var : 0;
    int nl = 0, nf = 0; int fdir[RF_FACES], fj0[RF_FACES];
    for (int side = 0; side < 2; side++) {                                /* the walls: the pocket reef's, the 1.8's depth */
        if (!(side ? VAR[var].wall_r : VAR[var].wall_l)) continue;
        for (int jj = 0; jj < gh; jj++) { const float v = jj / (float)gh; R->base[jj] = BXU * (0.07f + 0.13f * v * v) + BXU * 0.025f * bd_n1(jj * (0.08f / S), 70 + side); }
        fdir[nf] = side ? -1 : 1; fj0[nf] = (int)(gh * 0.03f) + (side ? (int)(4 * S) : 0);
        nl = rf_face(nf, fdir[nf], side ? gw - 1 : 0, fj0[nf], 1.0f, side, nl); nf++;
    }
    for (int q = 0; q < VAR[var].np; q++) {                               /* the pinnacles: two faces, back to back */
        const float cx = VAR[var].pin[q].cx * gw; const int jt = (int)(VAR[var].pin[q].top * gh);
        for (int d = 0; d < 2 && nf < RF_FACES; d++) {
            for (int jj = jt; jj < gh; jj++) { const float t = (float)(jj - jt) / (gh - jt); R->base[jj] = BXU * (VAR[var].pin[q].wt + (VAR[var].pin[q].wb - VAR[var].pin[q].wt) * t * sqrtf(t)) + BXU * 0.012f * bd_n1(jj * (0.09f / S), 80 + q * 2 + d); }
            fdir[nf] = d ? -1 : 1; fj0[nf] = jt + (d ? (int)(5 * S) : 0);
            nl = rf_face(nf, fdir[nf], cx, fj0[nf], 0.55f, 10 + q * 2 + d, nl); nf++;
        }
    }
    for (int f = 0; f < nf; f++) rf_boulders(f, fdir[f], fj0[f], f);       /* the stone: rounded boulders stamped over one another */
    /* the far rock: behind each wall a dimmer one reaching farther in; behind the floor a low crest, higher where a pinnacle stands */
    for (int j = 0; j < gh; j++) {
        const float v = (float)j / gh;
        R->k.farl[j] = VAR[var].wall_l ? (int16_t)(BXU * (0.20f + 0.10f * v + 0.06f * bd_n1(j * 0.04f, 61) + 0.03f * bd_n1(j * 0.18f, 62))) : -1;
        R->k.farr[j] = VAR[var].wall_r ? (int16_t)(gw - 1 - BXU * (0.20f + 0.10f * v + 0.06f * bd_n1(j * 0.04f, 63) + 0.03f * bd_n1(j * 0.18f, 64))) : (int16_t)gw;
    }
    for (int i = 0; i < gw; i++) {
        float top = 0.90f - 0.05f * bd_n1(i * 0.012f, 65) - 0.02f * bd_n1(i * 0.07f, 66);
        for (int q = 0; q < VAR[var].np; q++) { const float u = (i - (VAR[var].pin[q].cx + 0.045f) * gw) / (BXU * 0.16f), b = 1 - u * u; if (b > 0) top -= (1 - VAR[var].pin[q].top) * 0.42f * b * b; }
        R->k.fartop[i] = (int16_t)(gh * top);
    }
    /* the moss: along each ledge's top where nothing stands on it, cushions bumping up (two cells deep: the pocket reef's one) */
    for (int q = 0; q < nl; q++) {
        const ledge_t *l = &R->L[q]; const int j = l->j, m3 = (int)(3 * S);
        const int a = (int)(l->side ? l->tip : l->root - m3), b = (int)(l->side ? l->root + m3 : l->tip);
        for (int i = a < 0 ? 0 : a; i <= b && i < gw; i++) {
            if (j + 3 >= gh || !rw[j][i] || (j > 0 && (rw[j - 1][i] & 7) == RG_BODY)) continue;
            rw[j][i] = RG_TIP; if (rw[j + 1][i]) rw[j + 1][i] = RG_TIP;
            if (rw[j + 2][i]) rw[j + 2][i] = RG_GROOVE;
            if (rw[j + 3][i]) rw[j + 3][i] = RG_GROOVE;
            if (j > 2 && (bd_h(i / 4, j + 960) % 3) == 0) { rw[j - 1][i] = RG_TIP; rw[j - 2][i] = RG_TIP; if ((bd_h(i / 2, j + 961) & 3) == 0 && j > 4) { rw[j - 3][i] = RG_TIP; rw[j - 4][i] = RG_TIP; } }
        }
    }
    /* the rock's tone per cell, in place, top to bottom (the pocket reef's rules, at twice the reach: a cell here is half of one there) */
    for (int j = 0; j < gh; j++)
        for (int i = 0; i < gw; i++) {
            const uint8_t cell = rw[j][i];
            if (!cell) continue;
            int t; const int v = cell & 7;
            const bool up0 = j == 0 || !rw[j - 1][i], up1 = !up0 && (j == 1 || !rw[j - 2][i]);
            const bool dn = j >= gh - 2 || !rw[j + 1][i] || !rw[j + 2][i] || (j + 3 < gh && !rw[j + 3][i]);
            const bool under2 = (j + 4 < gh && !rw[j + 4][i]) || (j + 6 < gh && !rw[j + 6][i]);
            const bool side = (i > 1 && (!rw[j][i - 1] || !rw[j][i - 2])) || (i < gw - 2 && (!rw[j][i + 1] || !rw[j][i + 2]));
            const bool clear = i > 1 && i < gw - 2 && (j < 2 || (!rw[j - 2][i - 2] && !rw[j - 2][i + 2]));
            if (v == RG_TIP && !(cell & 0x80)) t = 5;                                             /* the moss cap and its cushions */
            else if (v == RG_GROOVE && !(cell & 0x80)) t = (bd_h(i / 2, j / 2 + 900) & 1) ? 6 : 5;  /* its second course, ragged */
            else if (j > 3 && (rw[j - 3][i] & 0x80) && (rw[j - 3][i] & 7) == 7 && (bd_h(i / 2, j / 2 + 901) % 3) == 0) t = 6;   /* (a few strands down the face) */
            else if ((up0 || up1) && clear) t = up0 ? 5 : (bd_h(i / 2, j + 902) & 1) ? 6 : 3;     /* a flat run of stone open above: moss */
            else if (up0 || up1) t = 3;                                                           /* a lone top: lit rock */
            else if (dn) t = 4;                                                                   /* the overhang's shadow */
            else if (under2) t = 2;
            else if (side) t = 3;
            else { const int l = (cell >> 4) & 3; t = l == 1 ? 1 : l == 2 ? 2 : l == 3 ? 4 : 0; }  /* the boulders' own shading */
            rw[j][i] = (uint8_t)(0x80 | (v == RG_GROOVE ? 7 : 0) | (uint8_t)t << 3);
        }
    /* the coral, on the ledges: each colony built on its own grid, toned, and laid into ct by cell */
    for (int q = 0; q < nl; q++) {
        const ledge_t *l = &R->L[q];
        const float v = l->j / (float)gh;
        if (v > 0.92f) continue;
        const bool low = v > 0.58f;
        const float span = l->side ? l->root - l->tip : l->tip - l->root;
        const int n = low ? 1 + (span > 8 * S) : 2 + (span > 9 * S);
        for (int c = 0; c < n; c++) {
            const uint32_t h = bd_h(q * 5 + c, 500 + var * 7);
            const int pick = (int)((h >> 3) % 100); int kind;
            if (low) kind = pick < 60 ? CK_BUTTONS : pick < 88 ? CK_FRONDS : CK_PLATES;
            else kind = pick < 36 ? CK_ANTLER : pick < 60 ? CK_FAN : pick < 78 ? CK_FRONDS : pick < 92 ? CK_BUTTONS : CK_PLATES;
            const int col = kind == CK_ANTLER ? ((h >> 12) & 1 ? 0 : 1) : kind == CK_FAN ? ((h >> 13) & 3 ? 2 : 0) : kind == CK_FRONDS ? 3 : kind == CK_BUTTONS ? 4 : 5;
            const float u = n == 1 ? 0.65f : 0.15f + 0.75f * c / (n - 1) + ((h >> 26) & 7) * 0.012f;
            const float xc = l->root + (l->tip - l->root) * u;
            float sc = (low ? 1.1f : 1.75f) * S * (0.85f + 0.3f * ((h >> 20) & 15) / 15.0f);
            if (span < 9 * S && !low) sc *= 0.8f;                              /* (a pinnacle's narrow ledge: a smaller colony) */
            rg_dir = l->side ? -1 : 1;
            rg_x0 = RG_GW; rg_y0 = RG_GH; rg_x1 = -1; rg_y1 = -1; rg_seed = h; rg_k = 0;
            const int oi = (int)xc - RG_GW / 2, oj = l->j + 3 - (RG_GH - 2), grp = q % 3;
            rg_colony(kind, sc);
            for (int fit = 0; fit < 3 && rg_x1 >= rg_x0 && oj + rg_y0 < 3; fit++) {   /* it would stand out of the water's top: the same colony, smaller, till it fits */
                for (int jj = rg_y0; jj <= rg_y1; jj++) memset(&rg[jj][rg_x0], 0, (size_t)(rg_x1 - rg_x0 + 1));
                sc *= 0.72f; rg_x0 = RG_GW; rg_y0 = RG_GH; rg_x1 = -1; rg_y1 = -1; rg_k = 0;
                rg_colony(kind, sc);
            }
            if (rg_x1 < rg_x0) continue;
            for (int j = rg_y0; j <= rg_y1; j++) {
                const int cj = oj + j; if ((unsigned)cj >= (unsigned)gh) continue;
                for (int i = rg_x0; i <= rg_x1; i++) {
                    const uint8_t cv = rg[j][i]; if (!cv) continue;
                    const int ci = oi + i; if ((unsigned)ci >= (unsigned)gw) continue;
                    const bool ol = i < 2 || !rg[j][i - 1] || !rg[j][i - 2], ou = j < 2 || !rg[j - 1][i] || !rg[j - 2][i];
                    const bool orr = i >= RG_GW - 2 || !rg[j][i + 1] || !rg[j][i + 2], od = j >= RG_GH - 2 || !rg[j + 1][i] || !rg[j + 2][i];
                    int t;
                    if (cv == RG_HOLE) t = T_HOLE;
                    else if (cv == RG_GROOVE) t = T_RIM;
                    else if (cv == RG_TIP) t = (orr || od) && !(ol || ou) ? T_RIM : T_TIP;
                    else if (ol || ou || orr || od) t = (orr || od) ? T_RIM : T_LIT;
                    else t = bd_h((i >> 1) + q * 7 + c, j >> 1) % 100 < 10 ? T_SHADE : T_BODY;
                    R->ct[cj][ci] = CT(t, col, grp);
                }
            }
            for (int jj = rg_y0; jj <= rg_y1; jj++) memset(&rg[jj][rg_x0], 0, (size_t)(rg_x1 - rg_x0 + 1));
        }
    }
    for (int c = 0; c < 6; c++) {                                         /* the corals' tones, once */
        const uint32_t b = RF_COL[c];
        R->k.tone[c][T_RIM] = mix(b, 0x140a24, 0.62f); R->k.tone[c][T_BODY] = b; R->k.tone[c][T_SHADE] = mix(b, 0x000000, 0.22f);
        R->k.tone[c][T_LIT] = mix(b, 0xffffff, 0.26f); R->k.tone[c][T_TIP] = mix(b, 0xffffff, 0.50f); R->k.tone[c][T_HOLE] = mix(b, 0x05080a, 0.75f);
    }
    for (int j = 0; j < gh; j++)                                          /* rock and coral into one byte a cell (the coral over the rock) */
        for (int i = 0; i < gw; i++) {
            const uint8_t c = R->ct[j][i], r = rw[j][i];
            rw[j][i] = c ? c : r ? (uint8_t)(7 | (r & 0x38)) : 0;
        }
    s_s->reef = &R->k;
    void *kp = keep_take(BG_REEF, sizeof(reef_keep_t));                   /* kept for the next rebuild, if the lender has room */
    if (kp) { memcpy(kp, &R->k, sizeof R->k); s_s->reef = kp; }
    s_s->ymin = 0;
}
static const uint32_t *reef_row(int y) {
    if (y >= BB) return NULL;
    const reef_keep_t *K = s_s->reef;
    row_water(y);
    const uint32_t w = s_s->wrow[y];
    uint32_t rk[7], cc[6][T_N];
    for (int k = 0; k < 7; k++) rk[k] = mix8(RK[k], w, 41);                 /* (the rock: 0.16 of the water) */
    for (int c = 0; c < 6; c++) for (int t = 0; t < T_N; t++) cc[c][t] = mix8(K->tone[c][t], w, 31);   /* (the coral: 0.12 of the water - saturated) */
    const uint32_t fc = bd_far(0x163232, y, 0.72f), fe = bd_far(0x26464a, y, 0.72f);   /* the faint far rock, its edge a little lit */
    const int fl = K->farl[y], fr = K->farr[y];
    const uint8_t *mr = K->mg[y];
    for (int x = 0; x < BW; x++) {
        const uint8_t m = mr[x];
        if (m) {
            if ((m & 7) == 7) { s_s->row[x] = rk[(m >> 3) & 7]; s_s->lay[x] = 2; }                                   /* the rock */
            else { s_s->row[x] = cc[(m >> 3) & 7][(m & 7) - 1]; s_s->lay[x] = (uint8_t)(3 + (m >> 6)); }             /* the coral over it */
            continue;
        }
        const int dt = y - K->fartop[x];
        if (x <= fl) { s_s->row[x] = x > fl - 2 ? fe : fc; s_s->lay[x] = 1; }
        else if (x >= fr) { s_s->row[x] = x < fr + 2 ? fe : fc; s_s->lay[x] = 1; }
        else if (dt >= 0) { s_s->row[x] = dt < 2 ? fe : fc; s_s->lay[x] = 1; }
    }
    return s_s->row;
}
