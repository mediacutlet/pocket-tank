/* bg_more.inc - THE 0.4.2 BACKGROUNDS (included by backgrounds.c; docs/backgrounds-0.4.2.md).
 *
 * Sunken Ship, Volcanic Vent, Mangroves: drawn row by row like the seven before them, their shapes STILL (baked: no
 * frame cost), nothing kept between bakes, every grid in the bake's borrowed scratch. Two things are new with them:
 * - pixels that LIGHT THEMSELVES (s_s->lit, a weight 0..255): the night dims them less - the vent's mouths, its hot
 *   rims and cracks (the volcano's embers' rule);
 * - THE DRIFT is no longer Twilight's alone (bg_speck): the vent's mouths throw embers, the mangroves' water carries
 *   silt and a falling leaf or two - the same few dozen marked specks a frame.
 * Strato's notes on the mock-ups (11-10) are the brief: the hull runs off the glass, it reads as a wreck (planks,
 * ribs through the breach, a broken rail, rigging, growth, haze with distance); mangroves stand dim in the distance
 * in murky water; the vents glow and spit.
 */
/* (BM_S - sizes in the 1.8's pixels, scaled to the canvas - is backgrounds.c's, with THE WIDE FRAME) */
/* the grain: a random byte a pixel from a 1 KB table, each row from its own random offset (a hash a pixel was the
   costliest thing in these rows on the S3's numbers) */
static void nz_fill(void) { for (int i = 0; i < 1024; i++) s_s->nz[i] = (uint8_t)bd_h(i, 7); }
static inline int nz_row(int y) { return (int)(bd_h(y, 5) & 1023); }
static inline uint32_t bd_far8(uint32_t rgb, int y, int h256) { return mix8(rgb, s_s->wrow[y], h256); }   /* bd_far in integers (h256 = haze x 256) */
#define NZ(x, ro) (s_s->nz[((x) + (ro)) & 1023])
static inline void lit_px(int x, int w) { if (w > s_s->lit[x]) s_s->lit[x] = (uint8_t)(w > 255 ? 255 : w); }

/* ================================ 8 SUNKEN SHIP ================================ */
/* an old wooden ship lying across the back, listing: her stern sunk in the sand and gone off the glass on the left
   (the quarterdeck stands a step higher there), her bow raised on a mound of rock on the right, the bowsprit still
   on it. Strakes of planks along the sheer, each plank its own tone, butt joints, a few planks gone (ribs behind);
   a breach amidships with the ribs standing in it; gunports, some lids still up; bulwarks on the castles, a broken
   rail between; the main mast leaning back with its top, a yard hanging askew and what is left of a sail; a stump of
   a foremast, a mizzen; shrouds with ratlines on one side, one parted; the forestay; the anchor chain hanging from
   the bow; weed on every edge, barnacles low on the hull. Farther along the hull the water takes more of it.
   Layers: the mound, the hull, its fittings, the masts and rigging */
static const uint32_t SH_WOOD = 0x80745e, SH_WOOD_DK = 0x2a261e, SH_RIM = 0xb4aa8c, SH_SEAM = 0x1a1814, SH_HOLE = 0x040707, SH_RIB = 0x5e5646,
                      SH_MOSS = 0x7c9c6c, SH_BARN = 0xc8c4ae, SH_CLOTH = 0xb8b49e, SH_ROCK = 0x2a2c28, SH_ROCK_LT = 0x5c6054, SH_ROPE = 0x8e8672, SH_MAST = 0x665e4c;
static void sh_line(float x0, float y0, float x1, float y1, int th) {
    __typeof__(s_s->u.sh) *H = &s_s->u.sh;
    if (H->nl >= SH_LN || y1 - y0 < 1) return;
    H->ln[H->nl].x0 = x0; H->ln[H->nl].y0 = y0; H->ln[H->nl].y1 = y1; H->ln[H->nl].sl = (x1 - x0) / (y1 - y0); H->ln[H->nl].th = (uint8_t)th; H->nl++;
}
static void sh_weed(float x, float y0, float len) {
    __typeof__(s_s->u.sh) *H = &s_s->u.sh;
    if (H->nw >= SH_WD || x < 1 || x >= BW - 2) return;
    H->wd[H->nw].x = (int16_t)x; H->wd[H->nw].y0 = (int16_t)y0; H->wd[H->nw].y1 = (int16_t)(y0 + len); H->nw++;
}
static void ship_begin(void) {
    __typeof__(s_s->u.sh) *H = &s_s->u.sh;
#ifdef TANK_P4                                    /* THE WIDE FRAME: the ship her own length (the 1.8's width at this height), more of her stern on the
                                                     glass, the mound and open water running on past her bow */
    const float W = BXU, X0 = (BW - BXU) * 0.30f;
#else
    const float W = BW, X0 = 0;
#endif
#define SX(f) (X0 + (f) * W)
    const float B = BB, xs = SX(-0.14f), xb = SX(0.86f), iL = 1 / (xb - xs);
    const int up_q = 7 * (int)(0.075f * B / 7 + 0.5f), up_f = 7 * (int)(0.045f * B / 7 + 0.5f);   /* the castles stand whole strakes higher */
    H->nl = 0; H->nw = 0; H->np = 0;
    H->xend = (int)xb < BW - 1 ? (int)xb : BW - 1; H->hy0 = BB; H->my0 = BB; H->mx0 = BW;
    for (int x = 0; x < BW; x++) {
        const float t = (x - SX(0.68f)) / (x < SX(0.68f) ? 0.30f * W : 0.62f * W), b = 1 - t * t;   /* the mound under the bow, off the glass on the right */
        H->mnd[x] = BB;
        if (b > 0) {
            H->mnd[x] = (int16_t)(B * (1.0f - 0.215f * b * b - 0.018f * bd_n1(x * 0.05f, 83) - 0.010f * bd_n1(x * 0.31f, 84)) + 3);
            if (H->mnd[x] < H->my0) H->my0 = H->mnd[x];
            if (x < H->mx0) H->mx0 = x;
        }
        H->deck[x] = H->keel[x] = H->dep[x] = -1; H->rail[x] = 0; H->sail[x] = 0;
        if (x > xb) continue;
        const float u = (x - xs) * iL, u2 = u * u, u4 = u2 * u2;
        const float yb = B * (0.72f - 0.20f * u - 0.04f * u4);                 /* listing: the stern down, the bow up */
        const float d = B * 0.30f * (1 - u4 * u2);                             /* the hull's depth, to a raked stem */
        const int up = u < 0.34f ? up_q : u > 0.86f ? up_f : 0;
        int k = (int)(yb + d); if (k > BB - 1) k = BB - 1;
        H->deck[x] = (int16_t)((int)yb - up); H->keel[x] = (int16_t)k;
        H->dep[x] = (int16_t)(k - H->deck[x] + 1); H->isd[x] = (8 << 16) / (H->dep[x] + 1);
        if (H->deck[x] < H->hy0) H->hy0 = H->deck[x];
        H->hn[x] = (uint8_t)(bd_n1(x * 0.21f, 81) * 255);
        const int q = (int)(u * 4.6f) - 1; H->hz[x] = (uint8_t)(q < 0 ? 0 : q > 3 ? 3 : q);
    }
    for (int k = 0; k < 8; k++) H->tone[k] = mix(SH_WOOD, SH_WOOD_DK, k / 7.0f * 0.9f);   /* lit at the deck, dark at the keel */
    for (int i = 0; i < BH + 8; i++) { H->m7[i] = (uint8_t)(i % 7); H->d7[i] = (uint8_t)(i / 7); }
    for (int i = 0; i < 1024; i++) H->pk[i] = (uint8_t)(((i / 84) & 31) | (i % 10 == 0 ? 0x20 : i % 10 < 3 ? 0x40 : 0) | (i % 84 == 0 ? 0x80 : 0));   /* the plank, a rib, a butt joint */
    for (int i = 0; i < 32; i++) H->jag[i] = (uint8_t)bd_h(i, 87);
    H->post = (int)(6 * BM_S); H->bulw = (int)(8 * BM_S);
    const int gap = (int)(13 * BM_S);
    for (int x = 0; x <= H->xend; x++) {                                      /* the castles' bulwarks, a board gone here and there */
        const float u = (x - xs) * iL;
        if (u < 0.34f || (u > 0.86f && u < 0.975f)) { const uint32_t h = bd_h(x / (int)(10 * BM_S + 1), 86); H->rail[x] = (h & 7) == 0 ? 0 : 3; }
    }
    for (int x = (int)(xs + 0.34f / iL) + 5, i = 0; x < xs + 0.86f / iL - 6; x += gap, i++) {   /* the waist's rail: posts, the rail between, broken */
        const uint32_t h = bd_h(i, 82);
        if ((h & 3) == 0) continue;                                           /* a post gone */
        for (int q = 0; q < 2; q++) H->rail[x + q] = 2;
        if ((h >> 4 & 3) != 0) for (int q = 2; q < gap && x + q < BW; q++) if (!H->rail[x + q]) H->rail[x + q] = 1;
    }
    H->hy0 -= H->bulw + 6;
    {                                                                         /* the breach, amidships */
        const int bx = (int)(xs + 0.60f / iL);
        H->bx = (float)bx; H->by = H->deck[bx] + H->dep[bx] * 0.50f; H->bbeam = (int)(H->dep[bx] * 0.44f);
        H->ibrx = 1 / (0.072f * W); H->ibry = 1 / (0.085f * B);
        H->bx0 = (int)(bx - 0.072f * W * 1.1f); H->bx1 = (int)(bx + 0.072f * W * 1.1f);
    }
    static const float PORT_U[] = { 0.40f, 0.47f, 0.69f, 0.76f, 0.83f, 0.90f };   /* the gunports (none in the breach) */
    for (int i = 0; i < (int)(sizeof PORT_U / sizeof PORT_U[0]) && H->np < SH_PT; i++) {
        const int x0 = (int)(xs + PORT_U[i] / iL), x1 = x0 + (int)(7 * BM_S); if (x0 < 0 || x1 > H->xend) continue;
        const int da = H->deck[x0] < H->deck[x1] ? H->deck[x0] : H->deck[x1], db = H->deck[x0] > H->deck[x1] ? H->deck[x0] : H->deck[x1];
        const int at = PORT_U[i] > 0.86f ? 22 : 15;                           /* (under the forecastle: a strake lower) */
        H->pt[H->np].x0 = (int16_t)x0; H->pt[H->np].x1 = (int16_t)x1; H->pt[H->np].ya = (int16_t)(da + at - 6); H->pt[H->np].yb = (int16_t)(db + at + 6);
        H->pt[H->np].at = (uint8_t)at; H->pt[H->np].open = (uint8_t)(bd_h(i, 85) & 1); H->np++;
    }
    /* the masts: main (leaning back, its top broken), the foremast's stump, the mizzen on the quarterdeck */
    static const struct { float xb, xt, top, w0, w1; } MA[3] = { { 0.36f, 0.295f, 0.055f, 1.6f, 3.2f }, { 0.64f, 0.626f, -0.15f, 2.2f, 2.9f }, { 0.085f, 0.05f, 0.24f, 1.2f, 2.4f } };
    for (int m = 0; m < 3; m++) {
        const int mb = (int)SX(MA[m].xb);
        H->ma[m].x1 = (float)mb; H->ma[m].y1 = (float)H->deck[mb];
        H->ma[m].x0 = SX(MA[m].xt); H->ma[m].y0 = MA[m].top < 0 ? H->ma[m].y1 + MA[m].top * B : MA[m].top * B;   /* (a stump: so far above the deck) */
        H->ma[m].w0 = MA[m].w0 * BM_S; H->ma[m].w1 = MA[m].w1 * BM_S; H->ma[m].inv = 1 / (H->ma[m].y1 - H->ma[m].y0);
    }
    s_s->ymin = (int)H->ma[0].y0;
    const float mx0 = H->ma[0].x0, my0 = H->ma[0].y0, mdx = H->ma[0].x1 - mx0, mdy = H->ma[0].y1 - my0;
    const float pcx = mx0 + mdx * 0.30f, pcy = my0 + mdy * 0.30f;             /* the top: a platform on the mast */
    H->plat_y = (int)pcy; H->plat_x0 = (int)(pcx - 6 * BM_S); H->plat_x1 = (int)(pcx + 6 * BM_S);
    H->yx = mx0 + mdx * 0.42f; H->yy = my0 + mdy * 0.42f; H->yhalf = 0.088f * W; H->ysl = 0.26f;   /* the yard, hanging askew */
    for (int x = (int)(H->yx - H->yhalf); x <= (int)(H->yx + H->yhalf); x++) {  /* what is left of the sail: rags of it */
        if (x < 0 || x >= BW) continue;
        const float n = bd_n1(x * 0.10f, 88);
        if (n > 0.40f) { int l = (int)((n - 0.28f) * 0.27f * B) + (int)(bd_h(x, 89) & 3); H->sail[x] = (uint8_t)(l > 250 ? 250 : l); }
    }
    const float tipx = SX(0.972f), tipy = 0.360f * B;                         /* the bowsprit's end */
    sh_line(mx0 + 1, my0 + 4, tipx, tipy, 0);                                 /* the forestay */
    { const int bxi = H->xend; sh_line(tipx, tipy, (float)bxi - 2, (float)H->deck[bxi] + 1, 2); }   /* the bowsprit */
    H->rat_a = H->nl;
    for (int i = 0; i < 3; i++) { const int rx = (int)SX(0.425f + 0.034f * i); sh_line(pcx + 4 * BM_S, pcy + 2, (float)rx, (float)(H->deck[rx] - H->post), 0); }   /* the shrouds, to the rail */
    H->rat_b = H->nl - 1; H->rat_y0 = (int)pcy + 8; H->rat_y1 = H->deck[(int)SX(0.425f)] - H->post - 3;
    { const int rx = (int)SX(0.262f); sh_line(pcx - 4 * BM_S, pcy + 2, (float)rx, (float)(H->deck[rx] - H->post), 0); }
    { const float ex = SX(0.225f), ey = (float)(H->deck[(int)SX(0.225f)] - H->post), sx = pcx - 5 * BM_S, sy = pcy + 2;   /* ... one parted, hanging */
      sh_line(sx, sy, sx + (ex - sx) * 0.52f, sy + (ey - sy) * 0.52f, 0); sh_weed(sx + (ex - sx) * 0.52f, sy + (ey - sy) * 0.52f, 20 * BM_S); }
    H->chx = (int)(xs + 0.93f / iL); H->chy0 = H->deck[H->chx] + 8; H->chy1 = H->mnd[H->chx];   /* the anchor chain, from the hawse to the rock */
    /* weed: off the yard's ends, the top, the mizzen, the bowsprit, the stay, the rail */
    sh_weed(H->yx - H->yhalf * 0.92f, H->yy - H->yhalf * 0.92f * H->ysl, 16 * BM_S); sh_weed(H->yx + H->yhalf * 0.97f, H->yy + H->yhalf * 0.97f * H->ysl, 34 * BM_S);
    sh_weed((float)H->plat_x0, pcy + 3, 13 * BM_S); sh_weed((float)H->plat_x1, pcy + 3, 9 * BM_S);
    sh_weed(H->ma[2].x0 + 1, H->ma[2].y0 + 2, 18 * BM_S);
    for (int i = 0; i < 4; i++) { const float f = 0.22f + 0.2f * i, x = (mx0 + 1) + (tipx - mx0 - 1) * f; const uint32_t h = bd_h(i, 92); sh_weed(x, (my0 + 4) + (tipy - my0 - 4) * f, (5 + (float)(h & 15)) * BM_S); }
    sh_weed(tipx - 6 * BM_S, tipy + 4, 15 * BM_S); sh_weed(tipx - 22 * BM_S, tipy + 11, 10 * BM_S);
    for (int i = 0; i < 9; i++) {
        const uint32_t h = bd_h(i, 93); const int x = (int)SX(0.03f + 0.092f * i + 0.03f * ((h & 255) / 255.0f));
        if (x <= H->xend && H->deck[x] >= 0) sh_weed((float)x, (float)(H->deck[x] - (H->rail[x] ? H->post : 0)), (6 + (float)(h >> 8 & 15)) * BM_S);
    }
#ifdef TANK_P4
#define AN_K (1.4f * BM_S)                         /* the anchor's size: one of its units, in the canvas's pixels */
    {   /* THE ANCHOR (0.4.3, the wide frame; Strato 11-21, of the open water past her bow: "an anchor would be nice over there"):
           an old iron anchor fallen on the mound's far slope, leaning, one arm dug in - its crown at (anx, any), its shank
           along (ans, -anc) - and the chain run on from the hawse's foot over the rock to its ring */
        const int ax = (int)(X0 + 1.03f * W) < BW - 40 ? (int)(X0 + 1.03f * W) : BW - 40;
        H->anx = (float)ax; H->any = (float)H->mnd[ax] + 3; H->anc = 0.921f; H->ans = 0.390f;   /* (leaning 23 degrees) */
        const int r = (int)(60 * AN_K);
        H->an_x0 = ax - r < 0 ? 0 : ax - r; H->an_x1 = ax + r > BW - 1 ? BW - 1 : ax + r; H->an_y0 = (int)H->any - r; H->an_y1 = (int)H->any + 6;
        sh_weed(H->anx + 40 * AN_K * H->ans - 12 * AN_K * H->anc, H->any - 40 * AN_K * H->anc - 12 * AN_K * H->ans + 2, 11 * BM_S);
    }
#endif
#undef SX
}
static const uint32_t *ship_row(int y) {
    if (y < s_s->ymin || y >= BB) return NULL;
    __typeof__(s_s->u.sh) *H = &s_s->u.sh;
    uint32_t *const R = s_s->row; uint8_t *const L = s_s->lay;
    row_water(y);
    const int ro = nz_row(y);
    if (y >= H->my0) {                                                       /* the mound */
        const uint32_t m = bd_far8(SH_ROCK, y, 132), ml = bd_far8(SH_ROCK_LT, y, 132), mg = bd_far8(0x464a40, y, 132);
        for (int x = H->mx0; x < BW; x++) { const int d = y - H->mnd[x]; if (d < 0) continue; R[x] = d < 2 ? ml : (NZ(x, ro) & 15) == 0 ? mg : m; L[x] = 1; }
    }
    if (y >= H->hy0) {                                                       /* the hull */
        static const int HZ[4] = { 70, 90, 110, 130 };                      /* the water takes more of her toward the bow */
        uint32_t sh[4][8], rim[4], moss[4];
        for (int q = 0; q < 4; q++) { for (int k = 0; k < 8; k++) sh[q][k] = bd_far8(H->tone[k], y, HZ[q]); rim[q] = bd_far8(SH_RIM, y, HZ[q]); moss[q] = bd_far8(SH_MOSS, y, HZ[q] + 10); }
        const uint32_t seam = bd_far8(SH_SEAM, y, 150), hole = bd_far8(SH_HOLE, y, 70), rib = bd_far8(SH_RIB, y, 140), ribl = bd_far8(SH_RIM, y, 150),
                       barn = bd_far8(SH_BARN, y, 140), grain = bd_far8(0x94896e, y, 100);
        const float bdy = (y - H->by) * H->ibry, bdy2 = bdy * bdy;
        const int bx0 = H->bx0, bx1 = H->bx1, post = H->post, bulw = H->bulw;
        for (int x = 0; x <= H->xend; x++) {
            const int dd = y - H->deck[x], q = H->hz[x], hn = H->hn[x];
            if (dd < 0) {                                                   /* above the deck: bulwark, rail, weed on the edge */
                const int r = H->rail[x];
                if (r == 3) { if (dd >= -bulw) { R[x] = dd == -bulw ? rim[q] : H->m7[x & 255] == 0 ? sh[q][4] : sh[q][2]; L[x] = 3; } }
                else if (r == 2) { if (dd >= -post) { R[x] = sh[q][2]; L[x] = 3; } }
                else if (r == 1) { if (dd == -post || dd == -post + 1) { R[x] = sh[q][1]; L[x] = 3; } }
                if (hn > 168 && dd >= -((hn - 168) >> 4) - (r == 3 ? bulw : 0) && dd < -(r == 3 ? bulw : 0)) { R[x] = moss[q]; L[x] = 3; }
                continue;
            }
            if (y > H->keel[x]) continue;
            const int s = H->d7[dd];
            if (x >= bx0 && x <= bx1 && bdy2 < 1.25f) {                      /* the breach: each strake broken at its own length, the ribs in the dark */
                const float bdx = (x - H->bx) * H->ibrx;
                if (bdx * bdx + bdy2 < 0.48f + H->jag[s & 31] * (0.62f / 255)) {
                    const int rb = H->pk[(x + (dd >> 2)) & 1023] & 0x60;
                    R[x] = rb == 0x20 ? ribl : rb ? rib : (dd == H->bbeam || dd == H->bbeam + 1) ? rib : hole; L[x] = 3; continue;
                }
            }
            int si = (dd * H->isd[x]) >> 16;
            const int pi = H->pk[(x + s * 37) & 1023], g = s_s->nz[((pi & 31) * 7 + s * 13) & 1023];
            uint32_t c;
            if (dd < 2) c = rim[q];
            else if (H->m7[dd] == 6) c = seam; else if (pi & 0x80) c = sh[q][7];
            else if ((g & 31) == 0 && s >= 2) { const int rb = H->pk[(x + (dd >> 2)) & 1023] & 0x60; c = rb == 0x20 ? ribl : rb ? rib : hole; }   /* a plank gone */
            else {
                si += (g >> 6) - 1; if (s == 1) si -= 2;                    /* each plank its own tone; the wale lighter */
                c = sh[q][si < 0 ? 0 : si > 7 ? 7 : si];
                const int n = NZ(x, ro);
                if ((n & 127) == 0) c = grain; else if ((n & 127) == 1 && dd * 2 > H->dep[x]) c = barn;   /* barnacles, low on the hull */
            }
            if (dd < 6 && hn > 150 && dd <= (hn - 150) >> 4) c = moss[q];     /* weed over the edge */
            R[x] = c; L[x] = 2;
        }
        for (int k = 0; k < H->np; k++) {                                    /* the gunports: dark, a lit sill over each, a lid still up on some */
            if (y < H->pt[k].ya || y > H->pt[k].yb) continue;
            const int at = H->pt[k].at;
            for (int x = H->pt[k].x0; x <= H->pt[k].x1; x++) {
                const int dd = y - H->deck[x], q = H->hz[x];
                if (dd >= at && dd < at + 6) { R[x] = hole; L[x] = 3; }
                else if (dd == at - 1) { R[x] = rim[q]; L[x] = 3; }
                else if (H->pt[k].open && dd >= at - 5 && dd < at - 1) { R[x] = sh[q][0]; L[x] = 3; }
            }
        }
        if (y >= H->chy0 && y <= H->chy1) { const int x = H->chx + ((y >> 1) & 1); R[x] = bd_far8(0x9a9a8c, y, 120); L[x] = 3; }   /* the chain */
    }
    const uint32_t mast = bd_far8(SH_MAST, y, 112), mlit = bd_far8(0x9a907a, y, 112), rope = bd_far8(SH_ROPE, y, 150), ratl = bd_far8(SH_ROPE, y, 185);
    if (y >= H->rat_y0 && y <= H->rat_y1 && (y - H->rat_y0) % 6 == 0) {       /* the ratlines */
        const float xa = H->ln[H->rat_a].x0 + H->ln[H->rat_a].sl * (y - H->ln[H->rat_a].y0), xb = H->ln[H->rat_b].x0 + H->ln[H->rat_b].sl * (y - H->ln[H->rat_b].y0);
        row_span((int)xa, (int)xb, ratl, 4);
    }
    for (int k = 0; k < H->nl; k++) {                                         /* stays, shrouds, the bowsprit: the row's whole run, no gaps */
        if (y < H->ln[k].y0 || y > H->ln[k].y1) continue;
        const float sl = H->ln[k].sl, xa = H->ln[k].x0 + sl * (y - H->ln[k].y0), xb = xa + sl;
        const int a = (int)(xa < xb ? xa : xb), b = (int)(xa < xb ? xb : xa), th = H->ln[k].th;
        row_span(a - th, b - (b > a ? 1 : 0) + th, th ? mast : rope, 4);
    }
    for (int m = 0; m < 3; m++) {                                             /* the masts */
        if (y < H->ma[m].y0 || y > H->ma[m].y1) continue;
        const float t = (y - H->ma[m].y0) * H->ma[m].inv, cx = H->ma[m].x0 + (H->ma[m].x1 - H->ma[m].x0) * t, w = H->ma[m].w0 + (H->ma[m].w1 - H->ma[m].w0) * t;
        int a = (int)(cx - w + 0.5f), b = (int)(cx + w - 0.5f);
        if (y < H->ma[m].y0 + 5) a = b - (y - (int)H->ma[m].y0) * (b - a) / 5;   /* the broken top */
        row_span(a, b, mast, 4); if (a >= 0 && a < BW) R[a] = mlit;
    }
    if (y >= H->plat_y - 4 && y <= H->plat_y + 2) {                            /* the top */
        if (y >= H->plat_y) row_span(H->plat_x0 + (y - H->plat_y), H->plat_x1 - (y - H->plat_y), y == H->plat_y ? mlit : mast, 4);
        else { row_span(H->plat_x0, H->plat_x0 + 1, mast, 4); row_span(H->plat_x1 - 1, H->plat_x1, mast, 4); }
    }
    {                                                                        /* the yard and the sail's rags */
        const float half = H->yhalf, sl = H->ysl, y0 = H->yy - half * sl, th = 1.4f * BM_S;
        if (y >= y0 - 3 && y <= H->yy + half * sl + 70 * BM_S) {
            const uint32_t cl = bd_far8(SH_CLOTH, y, 150), cd = bd_far8(0x8a8672, y, 150);
            int xa = (int)(H->yx - half), xb = (int)(H->yx + half); if (xa < 0) xa = 0; if (xb > BW - 1) xb = BW - 1;
            float yl = H->yy + (xa - H->yx) * sl;
            for (int x = xa; x <= xb; x++, yl += sl) {
                const float d = y - yl;
                if (d >= -th && d <= th) { R[x] = d < 0 ? mlit : mast; L[x] = 4; }
                else if (d > th && d <= H->sail[x] && (NZ(x, ro) & 7) != 0) { R[x] = ((x >> 1) + (x >> 3)) & 1 ? cl : cd; L[x] = 4; }
            }
        }
    }
#ifdef TANK_P4
    if (y >= H->an_y0 && y <= H->an_y1) {                                    /* the anchor: iron, a lit edge up its shank, rust */
        const uint32_t iron = bd_far8(0x4e5048, y, 84), ilit = bd_far8(0xa2a898, y, 84), rust = bd_far8(0x8a5632, y, 84);
        const float k = AN_K, c = H->anc, sn = H->ans;
        for (int x = H->an_x0; x <= H->an_x1; x++) {
            if (y > H->mnd[x] + 1) continue;                                 /* (what is under the rock's line is buried) */
            const float dx = x - H->anx, dy = H->any - y, u = (dx * sn + dy * c) / k, v = (dx * c - dy * sn) / k, av = v < 0 ? -v : v;   /* u up the shank, v across it */
            bool in = false, edge = false;
            if (u >= 0 && u <= 46 && av <= 1.5f) { in = true; edge = v < -0.4f; }                       /* the shank */
            else if (u >= 38.6f && u <= 41.4f && av <= 14) { in = true; edge = u > 40.4f; }               /* the stock */
            else if (av >= 12.5f && av <= 15 && u >= 38 && u <= 42) in = true;                            /* ... its ends' knobs */
            else { const float ru = u - 50.5f, d2 = ru * ru + v * v; if (d2 <= 20.3f && d2 >= 7.3f) in = true; }   /* the ring */
            if (!in && u >= -1.5f && u <= 17) {                                                        /* the arms: a curve up and out from the crown, a fluke on each */
                const float t = u < 0 ? 0 : u / 16.0f, armv = 19 * (t < 1 ? t * (2 - t) : 1) , d = av - armv;
                if (u <= 16 && d >= -1.6f && d <= 1.6f) { in = true; edge = d > 0.6f; }
                else if (u >= 9 && u <= 17 && av <= armv + 1 && av >= armv - (u - 9) * 0.75f - 1) in = true;   /* the fluke's palm, inside the arm's end */
            }
            if (!in) continue;
            R[x] = edge ? ilit : (NZ(x, ro) & 7) == 0 ? rust : iron; L[x] = 3;
        }
    }
    if (y >= H->my0) {                                                       /* the chain, lying over the rock from under the hawse to the ring */
        const float rx = H->anx + 50.5f * AN_K * H->ans, ry = H->any - 50.5f * AN_K * H->anc;
        const uint32_t ch = bd_far8(0x9a9a8c, y, 120);
        for (int x = H->chx + 1; x < (int)rx && x < BW; x++) {
            const float t = (float)(x - H->chx) / (rx - H->chx);
            const int yc = H->mnd[x] - 1 + (int)((ry + 4 - H->mnd[x]) * t * t * t * (t > 0.82f ? 1 : 0));   /* (on the rock, lifting to the ring at its end) */
            if (y == yc || (y == yc - 1 && ((x >> 1) & 1))) { R[x] = ch; L[x] = 3; }   /* (links: every second pair a row higher) */
        }
    }
#endif
    {                                                                        /* the weed, hanging */
        const uint32_t wc = bd_far8(SH_MOSS, y, 120);
        for (int k = 0; k < H->nw; k++) if (y >= H->wd[k].y0 && y <= H->wd[k].y1) { const int x = H->wd[k].x + (((y - H->wd[k].y0) >> 2) & 1); R[x] = wc; L[x] = 3; }
    }
    return R;
}

/* ================================ 9 EMBERS (the volcanic vent; BG_VENT) ================================ */
/* black chimneys rising from a rubble mound on the deep floor: fat at the foot, knobbly, ledges that catch the light
   and shade what is under them, pale crust in broken bands; young spires against the great chimney's flanks, far ones
   only shapes in the murk; tube worms in pale clumps on the mound. Each mouth GLOWS like magma - a bright lip, a low
   red glow hugging the rim, the rim hot under it, cracks running down, all self-lit - and a smoky plume, dull red at its root, leans off it with the
   current. A warm light round the great chimney. The embers are the drift's (bg_speck). Layers: the far shapes and
   the mound, far, mid, near */
#ifdef TANK_P4
/* THE WIDE FRAME (0.4.3; Strato 11-21: "the volcano stacks are distorted and stretched wide"): each chimney the width it
   has on the 1.8 (shares of BXU), and fifteen of them across the wider canvas, not ten spread thin - five far shapes, the
   great chimney left of the middle with its two young spires, more of the field running off to the right */
#define VE_FAR 5
static const struct { float x, wb, wt, h, haze, lean; uint8_t lay, emb; } VE_C[VE_N] = {
    { 0.030f, 0.050f, 0.016f, 0.33f, 0.80f,  0.010f, 1, 0 },
    { 0.205f, 0.055f, 0.016f, 0.56f, 0.82f, -0.010f, 1, 0 },
    { 0.335f, 0.050f, 0.015f, 0.47f, 0.80f,  0.010f, 1, 0 },
    { 0.652f, 0.050f, 0.015f, 0.52f, 0.82f, -0.008f, 1, 0 },
    { 0.842f, 0.052f, 0.016f, 0.42f, 0.80f,  0.010f, 1, 0 },
    { 0.112f, 0.085f, 0.030f, 0.46f, 0.58f,  0.020f, 2, 6 },
    { 0.768f, 0.080f, 0.028f, 0.40f, 0.58f, -0.020f, 2, 6 },
    { 0.958f, 0.075f, 0.026f, 0.34f, 0.58f, -0.015f, 2, 0 },
    { 0.268f, 0.100f, 0.034f, 0.32f, 0.38f,  0.010f, 3, 7 },
    { 0.596f, 0.090f, 0.030f, 0.30f, 0.38f, -0.010f, 3, 0 },
    { 0.398f, 0.042f, 0.014f, 0.46f, 0.20f,  0.012f, 4, 4 },   /* a young spire */
    { 0.470f, 0.140f, 0.042f, 0.66f, 0.16f, -0.030f, 4, 14 },  /* the great chimney */
    { 0.548f, 0.046f, 0.016f, 0.36f, 0.18f, -0.010f, 4, 4 },   /* another */
    { 0.700f, 0.110f, 0.036f, 0.24f, 0.12f,  0.000f, 4, 7 },
    { 0.885f, 0.060f, 0.020f, 0.30f, 0.14f,  0.008f, 4, 0 },
};
/* (48 embers from seven mouths, the pocket vent's own: the other three mouths glow and throw none - embers from ten
   mouths marked more of the glass a frame, about a frame a second on the Tab5, measured 11-21) */
#define VE_EMBERS (6 + 6 + 7 + 4 + 14 + 4 + 7)
#else
#define VE_FAR 3                                  /* (the first few: shapes only, behind the mound) */
static const struct { float x, wb, wt, h, haze, lean; uint8_t lay, emb; } VE_C[VE_N] = {
    { 0.045f, 0.050f, 0.016f, 0.33f, 0.80f,  0.010f, 1, 0 },
    { 0.270f, 0.055f, 0.016f, 0.56f, 0.82f, -0.010f, 1, 0 },
    { 0.455f, 0.050f, 0.015f, 0.47f, 0.80f,  0.010f, 1, 0 },
    { 0.160f, 0.085f, 0.030f, 0.46f, 0.58f,  0.020f, 2, 6 },
    { 0.820f, 0.080f, 0.028f, 0.38f, 0.58f, -0.020f, 2, 6 },
    { 0.360f, 0.100f, 0.034f, 0.32f, 0.38f,  0.010f, 3, 7 },
    { 0.528f, 0.042f, 0.014f, 0.46f, 0.20f,  0.012f, 4, 4 },   /* a young spire */
    { 0.620f, 0.140f, 0.042f, 0.66f, 0.16f, -0.030f, 4, 14 },  /* the great chimney */
    { 0.722f, 0.046f, 0.016f, 0.36f, 0.18f, -0.010f, 4, 4 },   /* another */
    { 0.930f, 0.110f, 0.036f, 0.24f, 0.12f,  0.000f, 4, 7 },
};
#define VE_EMBERS (6 + 6 + 7 + 4 + 14 + 4 + 7)
#endif
static const uint32_t VE_ROCK = 0x36322e, VE_LIT = 0x6a6258, VE_CRUST = 0xa8a08a, VE_SMOKE = 0x2a2826, VE_EMBER = 0xc83c14, VE_LIP = 0xffb24a, VE_MAGMA = 0xee5c1c, VE_DEEP = 0x8c1c08;
static void ve_mouth(int k, float *mx, int *yt) {                           /* a chimney's mouth: the layout's and the embers' */
    *yt = (int)(BB * (1 - VE_C[k].h));
    *mx = BW * VE_C[k].x + BXU * VE_C[k].lean + 2 * BM_S * (bd_n1(*yt * 0.05f, 102 + k * 8) - 0.5f);
}
static void vent_begin(void) {
    __typeof__(s_s->u.ve) *V = &s_s->u.ve;
    s_s->ymin = BB;
    for (int x = 0; x < BW; x++)                                          /* the rubble mound */
        V->mt[x] = (int16_t)(BB * (0.84f - 0.07f * bd_n1(x * 0.018f, 77) - 0.02f * bd_n1(x * 0.09f, 78) - 0.012f * bd_n1(x * 0.33f, 76)));
    V->mtop = BB; V->mlow = 0;
    for (int x = 0; x < BW; x++) { if (V->mt[x] < V->mtop) V->mtop = V->mt[x]; if (V->mt[x] + 2 > V->mlow) V->mlow = V->mt[x] + 2; }
    for (int k = 0; k < VE_N; k++) {
        const int yt = (int)(BB * (1 - VE_C[k].h)); V->yt[k] = yt;
        for (int y = 0; y < BH; y++) { V->cx[k][y] = -1; V->hw[k][y] = 0; }
        for (int y = yt; y < BB; y++) {
            float v = (float)(y - yt) / (BB - yt);
            float knob = 0.34f * (bd_n1(y * 0.09f, 100 + k * 8) - 0.5f) + 0.16f * (bd_n1(y * 0.31f, 101 + k * 8) - 0.5f);
            uint32_t g = bd_h(y / 5, 103 + k * 8); if ((g & 15) == 0 && v > 0.15f) knob += 0.35f;       /* a ledge */
            float hw = BXU * (VE_C[k].wt + (VE_C[k].wb - VE_C[k].wt) * v * v) * (1 + knob);
            V->cx[k][y] = (int16_t)(BW * VE_C[k].x + BXU * VE_C[k].lean * (1 - v) + 2 * BM_S * (bd_n1(y * 0.05f, 102 + k * 8) - 0.5f));
            V->hw[k][y] = (int16_t)(hw + 1);
        }
        int ph = (int)((BB - yt) * 0.48f + BB * 0.11f); if (ph > yt) ph = yt;   /* the plume's height */
        V->ph[k] = k < VE_FAR ? 0 : ph;
        if (yt - V->ph[k] < s_s->ymin) s_s->ymin = yt - V->ph[k];
        const float R = V->hw[k][yt] * 1.5f + 4 * BM_S;                       /* the mouth's glow: wide, and low */
        V->hr[k] = (int)R; V->hinv[k] = (int)(65536.0f / (R * R)); V->hu[k] = (int)(R * 0.55f) < 4 ? 4 : (int)(R * 0.55f);
    }
    s_s->ymin = 0;                                                            /* (every row its own: the glow by the row's table, not a mix a pixel in the water pass) */
    for (int i = 0; i < 64; i++) V->sn[i] = (int8_t)(sinf(i * 6.2832f / 64) * 127);
    for (int i = 0; i < 256; i++) V->ed[i] = (uint8_t)(bd_n1(i * 0.35f, 79) * 255);         /* the plume's ragged edge */
#ifdef TANK_P4
    static const float WX[] = { 0.060f, 0.165f, 0.225f, 0.330f, 0.365f, 0.520f, 0.640f, 0.735f, 0.815f, 0.925f };   /* the tube worms' clumps */
#else
    static const float WX[] = { 0.075f, 0.235f, 0.295f, 0.455f, 0.50f, 0.775f, 0.875f };   /* the tube worms' clumps */
#endif
    V->nw = 0; V->wtop = BB;
    for (int c = 0; c < (int)(sizeof WX / sizeof WX[0]); c++) {
        const uint32_t h = bd_h(c, 75); const int n = 5 + (int)(h & 3);
        for (int i = 0; i < n && V->nw < VE_WMAX; i++) {
            const uint32_t g = bd_h(c * 16 + i, 74);
            const int x = (int)(WX[c] * BW + (i - n * 0.5f) * 3 * BM_S + (g & 1)); if (x < 0 || x >= BW) continue;
            V->wx[V->nw] = (int16_t)x; V->wy1[V->nw] = (int16_t)(V->mt[x] + 2); V->wy0[V->nw] = (int16_t)(V->mt[x] - (6 + (int)(g >> 4 & 7) + (i == n / 2 ? 4 : 0)) * BM_S); if (V->wy0[V->nw] < V->wtop) V->wtop = V->wy0[V->nw]; V->nw++;
        }
    }
}
static const uint32_t *vent_row(int y) {
    if (y >= BB) return NULL;
    __typeof__(s_s->u.ve) *V = &s_s->u.ve;
    uint32_t *const R = s_s->row; uint8_t *const L = s_s->lay;
    row_water(y);
    for (int k = 0; k < VE_FAR; k++) {                                        /* far off: shapes in the murk, a dull spark at each mouth */
        const int yt = V->yt[k]; if (y < yt) continue;
        const int cx = V->cx[k][y], hw = V->hw[k][y];
        const uint32_t c = bd_far8(VE_ROCK, y, (int)(VE_C[k].haze * 256));
        row_span(cx - hw, cx + hw, c, 1);
        if (y < yt + 2) row_span(cx - 1 - (y - yt), cx + 1 + (y - yt), mix8(c, VE_EMBER, 170), 1);
    }
    if (y >= V->mtop) {                                                       /* the mound */
        const uint32_t m = bd_far8(VE_ROCK, y, 128), mk = bd_far8(0x55504a, y, 128), crust = bd_far8(VE_CRUST, y, 141);
        const int ro = nz_row(y);
        if (y >= V->mlow) { for (int x = 0; x < BW; x++) R[x] = m; for (int x = (ro & 31); x < BW; x += 29 + (NZ(x, ro) & 7)) R[x] = crust; memset(L, 1, BW); }   /* (under every crest: a fill, the crust sown) */
        else for (int x = 0; x < BW; x++) { int d = y - V->mt[x]; if (d < 0) continue; R[x] = d < 2 ? mk : (NZ(x, ro) & 31) == 0 ? crust : m; L[x] = 1; }
    }
    if (y >= V->wtop) {                                                       /* the tube worms: pale stalks, a soft tip - each one whole, from its own
                                                                                 tip above the crest (not from the mound's highest row: the review's 2) */
        const uint32_t tube = bd_far8(0xdcd6c2, y, 132), tip = bd_far8(0xe8765c, y, 110);
        for (int i = 0; i < V->nw; i++) if (y >= V->wy0[i] && y <= V->wy1[i]) { const int x = V->wx[i]; R[x] = y < V->wy0[i] + 2 ? tip : tube; L[x] = 1; }
    }
    for (int k = VE_FAR; k < VE_N; k++) {
        const int yt = V->yt[k], ph = V->ph[k];
        const float hz = VE_C[k].haze;
        const uint8_t lay = VE_C[k].lay;
        if (y < yt && y >= yt - ph) {                                         /* the plume: widening, leaning, thinning */
            const int d = yt - y;
            const float f = 1 - (float)d / ph, pw = V->hw[k][yt] * 0.7f + d * 0.24f;
            const float pcx = V->cx[k][yt] + d * 0.16f + 4 * BM_S * V->sn[(d / 3) & 63] / 127.0f;
            const int amax = (int)(f * (1 - 0.6f * hz) * 230);
            if (amax >= 6) {
            const uint32_t warm = d < 13 ? mix8(VE_SMOKE, 0x8e2c12, (13 - d) * 18) : VE_SMOKE;   /* lit dull red from below at its root, soon gone */
            const float Wd = pw * (0.55f + 0.45f * V->ed[(d + k * 40) & 255] / 255.0f);   /* ragged */
            const int c = (int)pcx, hw = (int)Wd, inv = (int)(65536.0f / (Wd * Wd + 1));
            int x0 = c - hw, x1 = c + hw; if (x0 < 0) x0 = 0; if (x1 > BW - 1) x1 = BW - 1;
            for (int x = x0; x <= x1; x++) {                                  /* (integers: a float a pixel was most of the vent's bake) */
                const int dx = x - c, q = 65536 - dx * dx * inv; if (q <= 0) continue;
                R[x] = mix8(R[x], warm, (amax * (q >> 8)) >> 8); L[x] = lay;
            }
            }
        }
        if (y >= yt) {                                                        /* the chimney */
            const int cx = V->cx[k][y], hw = V->hw[k][y], l = cx - hw, r = cx + hw, dy = y - yt;
            const int hot = dy < 24 ? (24 - dy) * 9 : 0;                      /* the rim, hot under the mouth */
            const int h8 = (int)(hz * 256);
            uint32_t lit = bd_far8(VE_LIT, y, h8), body = bd_far8(VE_ROCK, y, h8), dk = bd_far8(0x181614, y, h8), crust = bd_far8(VE_CRUST, y, h8), crd = bd_far8(0x6d6759, y, h8);
            if (dy > 2) {                                                     /* a ledge: lit on top, shade under it */
                const int w2 = V->hw[k][y - 2];
                if (hw + 2 < w2) { lit = mix8(lit, body, 160); body = mix8(body, dk, 130); } else if (hw > w2 + 2) { body = mix8(body, lit, 170); lit = mix8(lit, crust, 90); }
            }
            if (hot) { lit = mix8(lit, VE_EMBER, hot); body = mix8(body, VE_EMBER, hot * 2 / 3); }
            const bool band = V->ed[(y * 2 + k * 53) & 255] > 178;              /* broken bands of crust */
            const int w = r - l + 1, xa = l + (2 * w + 7) / 8, xb = l + (6 * w + 7) / 8, ro = nz_row(y + k * 97), rb = nz_row(y / 2 + k * 97);
            for (int x = l < 0 ? 0 : l; x <= r && x < BW; x++) {
                if (dy < 4 && dy < (V->ed[(x * 5 + k * 37) & 255] >> 6)) continue;   /* the rim's broken edge */
                uint32_t c = x < xa ? lit : x >= xb ? dk : body;
                if (x < xb) { if (band && (NZ(x / 3, rb) & 3)) c = x < xa ? crust : crd; else if ((NZ(x, ro) & 63) == 0) c = crust; }
                R[x] = c; L[x] = lay;
                if (hot && x < xb) lit_px(x, hot * 2 / 3);
            }
            if (hot) s_s->any_lit = true;
            if (lay >= 3 && dy > 2 && dy < V->hr[k] * 3 && (NZ(y, k * 31) & 3)) {   /* cracks running a short way down from the mouth, glowing */
                const int wgt = 230 - dy * 230 / (V->hr[k] * 3);
                for (int c = 0; c < (hw > 9 ? 2 : 1); c++) {
                    const int x = cx + (c ? hw / 3 : -hw / 5) + ((V->sn[(y * (5 + c * 2) + k * 11 + c * 23) & 63] * hw) >> 9);
                    if (x >= 0 && x < BW && (!c || dy < V->hr[k] * 2)) { R[x] = mix8(R[x], dy < 10 ? VE_MAGMA : VE_EMBER, wgt); lit_px(x, wgt); s_s->any_lit = true; }
                }
            }
        }
        {                                                                     /* the mouth: magma in the opening (Strato 11-11: the first halo read
                                                                                 as a light bulb on the stack). A low, wide, ragged red glow that hugs
                                                                                 the rim; the brightest thing a thin orange sliver along the lip */
            const int hr = V->hr[k], hu = V->hu[k], dy = y - yt, cx = V->cx[k][yt], mh = V->hw[k][yt];
            if (dy >= -hu && dy <= 3) {
                const int inv = V->hinv[k], sc = (int)((1 - 0.55f * hz) * 256);
                const int fy = dy <= 0 ? 256 + dy * 256 / (hu + 1) : 256 - dy * 64;      /* gone a few rows above the lip, sooner below it */
                int x0 = cx - hr, x1 = cx + hr; if (x0 < 0) x0 = 0; if (x1 > BW - 1) x1 = BW - 1;
                for (int x = x0; x <= x1; x++) {
                    const int dx = x - cx, q = 65536 - dx * dx * inv; if (q <= 0) continue;
                    int a = ((q >> 8) * fy >> 8) * (120 + (V->ed[(x * 7 + y * 3 + k * 19) & 255] >> 1)) >> 8;   /* an uneven edge, column by column */
                    a = ((a * (a + 256) >> 9) * sc >> 8) * 215 >> 8;                     /* 0..~210 */
                    if (a < 4) continue;
                    R[x] = mix8(R[x], mix8(VE_DEEP, VE_MAGMA, a + (a >> 2)), a); if (L[x] < lay) L[x] = lay; lit_px(x, a);
                }
                s_s->any_lit = true;
            }
            if (dy >= -2 && dy <= 3) {                                        /* the lip: where the rim's broken edge meets the opening */
                const int sc = (int)((1 - 0.55f * hz) * 256);
                for (int x = cx - mh < 0 ? 0 : cx - mh; x <= cx + mh && x < BW; x++) {
                    const int e = V->ed[(x * 5 + k * 37) & 255] >> 6;
                    const int dx = x < cx ? cx - x : x - cx, t = dx * 256 / (mh + 1);    /* hottest (and thickest) in the middle, red at the ends */
                    if (dy != e && dy != e - 1 && !(dy == e - 2 && t < 150)) continue;
                    const uint32_t c = dy == e ? mix8(VE_MAGMA, VE_DEEP, t) : dy == e - 2 ? mix8(VE_MAGMA, VE_DEEP, t) : mix8(VE_LIP, VE_MAGMA, t + (t >> 1) > 256 ? 256 : t + (t >> 1));
                    R[x] = mix8(R[x], c, sc); L[x] = lay; lit_px(x, (255 - (t >> 1)) * sc >> 8);
                }
                s_s->any_lit = true;
            }
        }
    }
    return R;
}
/* the embers (the drift): each chimney's mouth throws a few - white-hot as they leave it, then orange, then a grey
   mote the current carries off; they light themselves while they glow. A function of the clock alone. */
/* a speck fades out before the glass's side (render.c drops one whole within 3 px of it: it would pop) */
static inline int speck_edge(float x, int a) { const float m = x < BW * 0.5f ? x - 3.5f : BW - 5.5f - x; return m <= 0 ? 0 : m < 8 ? (int)(a * m * 0.125f) : a; }
static void vent_speck(int i, float T, float *x, float *y, int *alpha, bool *big, uint32_t *rgb, bool *lit) {
    /* T is tank_anim's lap (200 pi s): every period here goes into it a whole number of times - 300..112 lives a lap (2.1 .. 5.6 s),
       the wobbles whole hundredths of a radian a second - so nothing jumps where the lap wraps, on a tank of any age */
    static const float INVP[8] = { 300 / 628.3185307f, 252 / 628.3185307f, 216 / 628.3185307f, 190 / 628.3185307f, 166 / 628.3185307f, 146 / 628.3185307f, 128 / 628.3185307f, 112 / 628.3185307f };
    int k = VE_FAR, n = i;
    while (k < VE_N - 1 && n >= VE_C[k].emb) n -= VE_C[k++].emb;
    const uint32_t h = bd_h(i, 93);
    float mx; int yt; ve_mouth(k, &mx, &yt);
    float p = T * INVP[h & 7] + (h >> 3 & 255) * (1 / 256.0f); p -= floorf(p);
    const float sz = VE_C[k].lay >= 4 ? 1.0f : 0.7f, rise = (30 + (float)(h >> 11 & 31)) * BM_S * sz * (VE_C[k].emb > 10 ? 1.5f : 1.0f), d = p * rise;
    *y = yt - 1 - d;
    *x = mx + ((int)(h >> 16 & 15) - 7.5f) * (VE_C[k].wt * BXU * 0.11f) + d * 0.16f + (1.0f + 3.5f * p) * BM_S * sinf(T * (1.2f + (h >> 20 & 7) * 0.22f) + (float)(h >> 23 & 63));
    const float e = 1 - p;
    *big = (h >> 30) == 0 && p < 0.5f;
    *lit = p < 0.62f;
    *rgb = p < 0.20f ? VE_LIP : p < 0.62f ? 0xf06a28 : 0x8c7068;
    *alpha = speck_edge(*x, (int)((p < 0.62f ? 255 : 150) * e * (0.4f + 0.6f * e) * (1 - 0.5f * VE_C[k].haze)));
}

/* ================================ 10 MANGROVES ================================ */
/* a mangrove wood seen from under the water, in three depths: FAR, a row of small trees dim in the murk, their roots
   coming down into a far mud bank; MID, two trees; NEAR, a tree at each side of the glass, its trunk coming down
   from above the water and its prop roots arching out and down into the mud, one forking, bark rings and oysters on
   them. Fine roots hang straight; pencil roots stand up out of the mud banks. The water is murky - olive, silt in
   it - and the light comes down between the trees in soft shafts and is gone by the floor. Silt and a leaf or two
   drift (bg_speck). Every root is a curve in y alone (x = x0 + reach (1 - (1 - s)^2)): no tables. Layers: far, mid, near */
static const uint32_t MG_ROOT = 0x2c2418, MG_LIT = 0x6e5e42, MG_DARK = 0x130e09, MG_MUD = 0x2a2a1c, MG_OYST = 0xbcbaa2;
/* kind (the .trunk field): 0 a prop root, 1 a trunk, 2 the trunk's own end running on down as a root. id: its wobble's phase (the
   numbers the approved roots had by their order, kept now the order is roots first, trunk over them) */
static void mg_root(float x0, float y0, float y1, float reach, float r0, float r1, int lay, int trunk, int id) {
    __typeof__(s_s->u.mg) *M = &s_s->u.mg;
    if (M->n >= MG_MAX || y1 - y0 < 2) return;
    M->r[M->n].x0 = x0; M->r[M->n].reach = reach; M->r[M->n].inv = 1 / (y1 - y0); M->r[M->n].r0 = r0 * BM_S; M->r[M->n].r1 = r1 * BM_S;
    M->r[M->n].y0 = (int16_t)y0; M->r[M->n].y1 = (int16_t)(y1 > BB - 1 ? BB - 1 : y1); M->r[M->n].lay = (uint8_t)lay; M->r[M->n].trunk = (uint8_t)trunk;
    M->r[M->n].ph = (uint8_t)bd_h(id, 96); M->n++;
}
static void mangrove_begin(void) {
    __typeof__(s_s->u.mg) *M = &s_s->u.mg;
    const float W = BW, B = BB;
    M->n = 0; M->nd = 0; s_s->ymin = 0;
    for (int i = 0; i < 64; i++) M->sn[i] = (int8_t)(sinf(i * 6.2832f / 64) * 127);
    M->ybank[0] = M->ybank[1] = BB;
    for (int x = 0; x < BW; x++) {                                        /* the mud banks, and the pencil roots standing out of them */
        const uint32_t h = bd_h(x, 99);
        M->bank[0][x] = (int16_t)(B * (0.872f - 0.014f * bd_n1(x * 0.021f, 97)));
        M->bank[1][x] = (int16_t)(B * (0.935f - 0.018f * bd_n1(x * 0.016f, 98)));
        M->pen[0][x] = (uint8_t)((h & 7) == 0 ? 2 + (h >> 3 & 3) : 0);
        M->pen[1][x] = (uint8_t)((h >> 8 & 7) == 0 ? (4 + (h >> 11 & 7)) * BM_S : 0);
        for (int b = 0; b < 2; b++) if (M->bank[b][x] - M->pen[b][x] < M->ybank[b]) M->ybank[b] = M->bank[b][x] - M->pen[b][x];
    }
#ifdef TANK_P4
    /* THE WIDE FRAME (0.4.3; Strato 11-21): every root's reach is the 1.8's (a share of BXU, not of the wider canvas), and the
       wood is wider - nine far trees, three in the middle distance, the near pair a root's reach in from each side. The far
       trees' roots leave the stem WHERE THE STEM IS at their height (they left the line it started on, and a leaning stem stood
       a root's width off them: "some branches do not line up with the root mast") */
    const float XU = BXU;
    for (int t = 0; t < 9; t++) {                                             /* FAR: nine small trees across the back */
        const uint32_t h = bd_h(t, 94);
        const float tx = (t + 0.5f + ((h & 255) / 255.0f - 0.5f) * 0.6f) * W / 9, ys = B * (0.36f + 0.10f * ((h >> 8 & 255) / 255.0f));
        const float lean = ((int)(h >> 16 & 15) - 7.5f) * BM_S, yend = ys + B * 0.10f;
        for (int j = 0; j < 4; j++) {
            const uint32_t g = bd_h(t * 8 + j, 95); const float side = (j & 1) ? 1.0f : -1.0f, y0 = ys + B * 0.028f * j;
            mg_root(tx + lean * (y0 + 4) / (yend + 4), y0, B * 0.90f, side * XU * (0.085f - 0.014f * j) * (0.75f + 0.5f * ((g & 255) / 255.0f)), 1.7f, 0.8f, 1, 0, t * 5 + 1 + j);
        }
        mg_root(tx + lean, yend, B * 0.90f, lean * 0.5f, 1.7f, 0.8f, 1, 2, 100 + t);
        mg_root(tx, -4, yend, lean, 2.0f, 1.7f, 1, 1, t * 5);
    }
    static const float MIDX[3] = { 0.315f, 0.535f, 0.735f };
    for (int t = 0; t < 3; t++) {                                             /* MID: three trees */
        const float tx = MIDX[t] * W;
        const float lean = ((t & 1) ? -5 : 4) * BM_S, yend = B * 0.36f;
        for (int j = 0; j < 6; j++) {
            const uint32_t g = bd_h(t * 8 + j, 90); const float side = ((j + t) & 1) ? 1.0f : -1.0f, y0 = B * (0.15f + 0.036f * j);
            mg_root(tx + lean * (y0 + 4) / (yend + 4), y0, B * (0.95f + 0.03f * ((g >> 8 & 255) / 255.0f)), side * XU * (0.135f - 0.017f * j) * (0.75f + 0.5f * ((g & 255) / 255.0f)), 3.2f, 1.4f, 2, 0, 30 + t * 7 + 1 + j);
        }
        mg_root(tx + lean, yend, B * 0.96f, lean * 1.5f, 3.2f, 1.4f, 2, 2, 110 + t);
        mg_root(tx, -4, yend, lean, 4.4f, 3.2f, 2, 1, 30 + t * 7);
    }
    const float NEARX[2] = { 0.105f * XU, W - 0.095f * XU };
    for (int t = 0; t < 2; t++) {                                             /* NEAR: a tree at each side of the glass */
        const float tx = NEARX[t], in = t ? -1.0f : 1.0f;
        static const uint8_t ID[7] = { 1, 3, 4, 6, 7, 8, 9 };
        for (int j = 0; j < 7; j++) {
            const uint32_t g = bd_h(t * 8 + j, 91); const float side = (j & 1) ? -in : in;   /* (the first, the widest, arches IN over the tank) */
            const float y0 = B * (0.03f + 0.046f * j), reach = side * XU * (0.235f - 0.027f * j) * (0.8f + 0.4f * ((g & 255) / 255.0f)), y1 = B * (0.99f + 0.06f * ((g >> 8 & 255) / 255.0f));
            if (j == 0 || j == 2) {                                           /* a fork: a second root leaving it half way down (laid first: its parent lies over where it leaves) */
                const float s = 0.42f, fy = y0 + (y1 - y0) * s, fx = tx + reach * (1 - (1 - s) * (1 - s));
                mg_root(fx, fy, B * 1.02f, -side * XU * 0.05f, 3.0f, 1.8f, 3, 0, 44 + t * 10 + ID[j] + 1);
            }
            mg_root(tx, y0, y1, reach, 5.4f, 2.2f, 3, 0, 44 + t * 10 + ID[j]);
        }
        mg_root(tx + in * 4 * BM_S, B * 0.34f, B * 1.02f, in * 0.02f * XU, 5.4f, 2.2f, 3, 2, 120 + t);   /* the trunk's own end, on down into the mud */
        mg_root(tx, -4, B * 0.34f, in * 4 * BM_S, 8.0f, 5.4f, 3, 1, 44 + t * 10);   /* the trunk LAST: every root comes out from behind it */
    }
    const struct { float x; uint8_t n, lay; } HANG[5] = { { MIDX[0] * W, 5, 2 }, { MIDX[1] * W, 5, 2 }, { MIDX[2] * W, 5, 2 }, { NEARX[0], 7, 3 }, { NEARX[1], 7, 3 } };   /* the fine roots, hanging straight */
    for (int t = 0; t < 5; t++)
        for (int j = 0; j < HANG[t].n && M->nd < MG_DMAX; j++) {
            const uint32_t h = bd_h(t * 16 + j, 92);
            M->dx[M->nd] = (int16_t)(HANG[t].x + (((int)(h & 255) - 128) / 128.0f) * 0.15f * XU);
            M->dl[M->nd] = (int16_t)(B * (0.12f + 0.40f * ((h >> 8 & 255) / 255.0f)));
            M->dlay[M->nd] = HANG[t].lay; M->dph[M->nd] = (uint8_t)(h >> 16); M->nd++;
        }
}
#else
    for (int t = 0; t < 6; t++) {                                             /* FAR: six small trees across the back */
        const uint32_t h = bd_h(t, 94);
        const float tx = (t + 0.5f + ((h & 255) / 255.0f - 0.5f) * 0.6f) * W / 6, ys = B * (0.36f + 0.10f * ((h >> 8 & 255) / 255.0f));
        const float lean = ((int)(h >> 16 & 15) - 7.5f) * BM_S;
        for (int j = 0; j < 4; j++) {
            const uint32_t g = bd_h(t * 8 + j, 95); const float side = (j & 1) ? 1.0f : -1.0f;
            mg_root(tx, ys + B * 0.028f * j, B * 0.90f, side * W * (0.085f - 0.014f * j) * (0.75f + 0.5f * ((g & 255) / 255.0f)), 1.7f, 0.8f, 1, 0, t * 5 + 1 + j);
        }
        mg_root(tx + lean, ys + B * 0.10f, B * 0.90f, lean * 0.5f, 1.7f, 0.8f, 1, 2, 100 + t);
        mg_root(tx, -4, ys + B * 0.10f, lean, 2.0f, 1.7f, 1, 1, t * 5);
    }
    static const float MIDX[2] = { 0.37f, 0.69f };
    for (int t = 0; t < 2; t++) {                                             /* MID: two trees */
        const float tx = MIDX[t] * W;
        const float lean = (t ? -5 : 4) * BM_S;
        for (int j = 0; j < 6; j++) {
            const uint32_t g = bd_h(t * 8 + j, 90); const float side = ((j + t) & 1) ? 1.0f : -1.0f;
            mg_root(tx, B * (0.15f + 0.036f * j), B * (0.95f + 0.03f * ((g >> 8 & 255) / 255.0f)), side * W * (0.135f - 0.017f * j) * (0.75f + 0.5f * ((g & 255) / 255.0f)), 3.2f, 1.4f, 2, 0, 30 + t * 7 + 1 + j);
        }
        mg_root(tx + lean, B * 0.36f, B * 0.96f, lean * 1.5f, 3.2f, 1.4f, 2, 2, 110 + t);
        mg_root(tx, -4, B * 0.36f, lean, 4.4f, 3.2f, 2, 1, 30 + t * 7);
    }
    static const float NEARX[2] = { 0.105f, 0.905f };
    for (int t = 0; t < 2; t++) {                                             /* NEAR: a tree at each side of the glass */
        const float tx = NEARX[t] * W, in = t ? -1.0f : 1.0f;
        static const uint8_t ID[7] = { 1, 3, 4, 6, 7, 8, 9 };
        for (int j = 0; j < 7; j++) {
            const uint32_t g = bd_h(t * 8 + j, 91); const float side = (j & 1) ? -in : in;   /* (the first, the widest, arches IN over the tank) */
            const float y0 = B * (0.03f + 0.046f * j), reach = side * W * (0.235f - 0.027f * j) * (0.8f + 0.4f * ((g & 255) / 255.0f)), y1 = B * (0.99f + 0.06f * ((g >> 8 & 255) / 255.0f));
            if (j == 0 || j == 2) {                                           /* a fork: a second root leaving it half way down (laid first: its parent lies over where it leaves) */
                const float s = 0.42f, fy = y0 + (y1 - y0) * s, fx = tx + reach * (1 - (1 - s) * (1 - s));
                mg_root(fx, fy, B * 1.02f, -side * W * 0.05f, 3.0f, 1.8f, 3, 0, 44 + t * 10 + ID[j] + 1);
            }
            mg_root(tx, y0, y1, reach, 5.4f, 2.2f, 3, 0, 44 + t * 10 + ID[j]);
        }
        mg_root(tx + in * 4 * BM_S, B * 0.34f, B * 1.02f, in * 0.02f * W, 5.4f, 2.2f, 3, 2, 120 + t);   /* the trunk's own end, on down into the mud */
        mg_root(tx, -4, B * 0.34f, in * 4 * BM_S, 8.0f, 5.4f, 3, 1, 44 + t * 10);   /* the trunk LAST: every root comes out from behind it (Strato 11-11:
                                                                                 the roots met the stem in a hard flat edge) */
    }
    static const struct { float x; uint8_t n, lay; } HANG[4] = { { 0.37f, 5, 2 }, { 0.69f, 5, 2 }, { 0.105f, 7, 3 }, { 0.905f, 7, 3 } };   /* the fine roots, hanging straight */
    for (int t = 0; t < 4; t++)
        for (int j = 0; j < HANG[t].n && M->nd < MG_DMAX; j++) {
            const uint32_t h = bd_h(t * 16 + j, 92);
            M->dx[M->nd] = (int16_t)(HANG[t].x * W + (((int)(h & 255) - 128) / 128.0f) * 0.15f * W);
            M->dl[M->nd] = (int16_t)(B * (0.12f + 0.40f * ((h >> 8 & 255) / 255.0f)));
            M->dlay[M->nd] = HANG[t].lay; M->dph[M->nd] = (uint8_t)(h >> 16); M->nd++;
        }
}
#endif
static const uint32_t *mangrove_row(int y) {
    if (y >= BB) return NULL;
    __typeof__(s_s->u.mg) *M = &s_s->u.mg;
    uint32_t *const R = s_s->row; uint8_t *const L = s_s->lay;
    row_water(y);
    const int ro = nz_row(y);
    static const int HZ[4] = { 0, 152, 116, 26 };                             /* the murk takes the far ones */
    for (int lay = 1; lay <= 3; lay++) {
#ifdef TANK_P4
        const uint32_t lit = bd_far8(MG_LIT, y, HZ[lay]), body = bd_far8(MG_ROOT, y, HZ[lay]), dk = bd_far8(MG_DARK, y, HZ[lay]), oy = mix8(bd_far8(MG_OYST, y, HZ[lay] + 40), body, 120);
        const uint32_t ringl = mix8(lit, body, 110), ringb = mix8(body, dk, 110);
#else
        const uint32_t lit = bd_far8(MG_LIT, y, HZ[lay]), body = bd_far8(MG_ROOT, y, HZ[lay]), dk = bd_far8(MG_DARK, y, HZ[lay]), oy = bd_far8(MG_OYST, y, HZ[lay] + 40);
#endif
        if (lay >= 2) for (int d = 0; d < M->nd; d++) {
            if (M->dlay[d] != lay || y > M->dl[d]) continue;
            const int x = M->dx[d] + (int)(1.5f * BM_S * M->sn[((y * 46) / 100 + M->dph[d]) & 63] / 127.0f);
            if (x >= 0 && x < BW) { R[x] = body; L[x] = (uint8_t)lay; }
            if (lay == 3 && x + 1 < BW && x >= 0) { R[x + 1] = dk; L[x + 1] = (uint8_t)lay; }
        }
        for (int k = 0; k < M->n; k++) {
            if (M->r[k].lay != lay || y < M->r[k].y0 || y > M->r[k].y1) continue;
            const float s = (y - M->r[k].y0) * M->r[k].inv, q = 1 - s, q2 = q - M->r[k].inv;
            const bool trunk = M->r[k].trunk == 1;
            /* THE JOIN: where a root leaves its stem (or a fork its root) it is laid UNDER it, a little wider there - a shoulder that follows
               the root's own angle out from behind the stem - its wobble held still, and shaded as the stem's side it comes out of; a
               trunk narrows to a root's width at its end and runs on down as one. No flat cut where two of them meet. */
            const float fl = M->r[k].trunk || s >= 0.10f ? 0 : 1 - s * 10, ease = s < 0.10f ? s * 10 : 1;
            const float wob = trunk ? 0 : ease * 1.6f * BM_S * M->sn[((y * 3 >> 3) + M->r[k].ph) & 63] * (1 / 127.0f);
            const float xa = M->r[k].x0 + M->r[k].reach * (trunk ? s : 1 - q * q) + wob, xb = trunk ? xa : M->r[k].x0 + M->r[k].reach * (1 - q2 * q2) + wob;
            const float hw = trunk ? (s < 0.9f ? M->r[k].r0 : M->r[k].r0 + (M->r[k].r1 - M->r[k].r0) * (s - 0.9f) * 10)   /* (wider than any shoulder till its last root has left) */
                                   : M->r[k].r0 + (M->r[k].r1 - M->r[k].r0) * s + 0.35f * M->r[k].r0 * fl * fl;
            int l = (int)((xa < xb ? xa : xb) - hw + 0.5f), r = (int)((xa < xb ? xb : xa) + hw - 0.5f); if (r < l) r = l;
            if (r < 0 || l > BW - 1) continue;
            const int w = r - l + 1;
            if (lay == 1) { row_span(l, r, body, 1); continue; }              /* far: a shape */
            const bool ring = lay == 3 && fl == 0 && ((y + M->r[k].ph) & 31) == 0, oyst = lay == 3 && !trunk && s > 0.70f;
            int xl = l + (2 * w + 7) / 8, xd = l + (6 * w + 7) / 8;
            if (fl > 0) { if (M->r[k].reach > 0) xd -= (int)((xd - l) * fl); else xl += (int)((r + 1 - xl) * fl); }   /* out of the stem's dark side, or its lit one */
            for (int x = l < 0 ? 0 : l; x <= r && x < BW; x++) {
#ifdef TANK_P4                                    /* (the big screens, Strato 11-21: the bark's rings a soft shade, not a ruled line; the oysters "too bright and white" - duller, and fewer) */
                uint32_t c = x < xl ? (ring ? ringl : lit) : x >= xd ? dk : (ring ? ringb : body);
                if (oyst && (NZ(x, ro) & 31) == 0) c = oy;
#else
                uint32_t c = x < xl ? (ring ? body : lit) : x >= xd ? dk : (ring ? dk : body);
                if (oyst && (NZ(x, ro) & 15) == 0) c = oy;
#endif
                R[x] = c; L[x] = (uint8_t)lay;
            }
        }
        if (lay <= 2 && y >= M->ybank[lay - 1]) {                             /* the mud bank, its crest a little lit, pencil roots out of it */
            const int16_t *bk = M->bank[lay - 1]; const uint8_t *pn = M->pen[lay - 1];
            const uint32_t mud = bd_far8(MG_MUD, y, lay == 1 ? 190 : 120), crest = bd_far8(0x4a4a34, y, lay == 1 ? 190 : 120), pr = lay == 1 ? body : dk;
            for (int x = 0; x < BW; x++) {
                const int d = y - bk[x];
                if (d >= 0) { R[x] = d < 2 ? crest : mud; L[x] = (uint8_t)lay; }
                else if (d >= -pn[x]) { R[x] = pr; L[x] = (uint8_t)lay; }
            }
        }
    }
    return R;
}
/* the drift: silt hanging in the water, sinking slowly, and now and then a leaf coming down, rocking as it falls */
static void mangrove_speck(int i, float T, float *x, float *y, int *alpha, bool *big, uint32_t *rgb, bool *lit) {
    const uint32_t h = bd_h(i, 95);
    const bool leaf = (h >> 13 & 7) == 0;
    const float H = (float)BB - 24;                                    /* (T is tank_anim's lap: each falls its height a whole number of times a lap -
                                                                                on the 1.8 silt at 0.5 .. 2.2 px/s, a leaf at 5 .. 9 - and sways in whole hundredths of a radian a second) */
#ifdef TANK_TAB5                                  /* (THE DRIFT IN STRIPS, backgrounds.c: the silt - a leaf falls too fast to step) */
    if (!leaf) T = speck_strip_clock(T, (int)(h & 1023));
#endif
    const float vy = H * (leaf ? 9 + (int)(h >> 20 & 7) : 1 + (int)(h >> 20 & 3)) * (1 / 628.3185307f);
#ifdef TANK_P4                                    /* (the big screens: the sines by table - backgrounds.c, drift_sin) */
    *y = drift_wrap((h >> 10 & 1023) * (H / 1024.0f) + vy * T, H);
    *x = (h & 1023) * (BW / 1024.0f) + (leaf ? 9.0f : 3.0f) * drift_sin(T * (leaf ? 0.9f : 0.17f + (h >> 24 & 7) * 0.02f) + (float)(h >> 27));
    *big = leaf; *lit = false;
    *rgb = leaf ? 0xe0cc7a : 0xccd0a8;
    *alpha = speck_edge(*x, leaf ? 235 : (int)(150 * (0.65f + 0.35f * drift_sin(T * 0.7f + (float)(h >> 5 & 63)))));
#else
    *y = fmodf((h >> 10 & 1023) * (H / 1024.0f) + vy * T, H);
    *x = (h & 1023) * (BW / 1024.0f) + (leaf ? 9.0f : 3.0f) * sinf(T * (leaf ? 0.9f : 0.17f + (h >> 24 & 7) * 0.02f) + (float)(h >> 27));
    *big = leaf; *lit = false;
    *rgb = leaf ? 0xe0cc7a : 0xccd0a8;
    *alpha = speck_edge(*x, leaf ? 235 : (int)(150 * (0.65f + 0.35f * sinf(T * 0.7f + (h >> 5 & 63)))));
#endif
}
