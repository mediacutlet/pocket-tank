/* Blackwater (2026-10-10): the wreck slot is a sunken WWII FIGHTER. Redrawn the same day so it
   reads as a plane at tank scale (the first cut was dark olive on near-black water and came
   out an unnameable shape): bare ALUMINIUM, lit from the upper left, a 1 px dark outline round
   every piece and between the pieces, big simple features. A single-engine radial fighter on
   the sand, nose to the right and a little nose-up: the near wing sweeps down toward the
   viewer onto the sand with a big tricolour roundel; the far wing rises behind the fuselage;
   the fin at the left with a red rudder and a white stripe; a yellow band round the rear
   fuselage; the canopy a pale frame round the cockpit (a HOLE the fish pass behind); a torn
   gap in the rear fuselage (the second hole); the propeller as two long blades on the spinner,
   one up (bent back, yellow tip) and one dug into the sand. Same layers as the other hulls:
   -1 = all of it, 0 = only the dark inside (baked, never in the front mask), 1 = the solid
   pieces (the mask). Local coordinates: x from the centre, h up from WK_FY. */
static inline float wkb_axis(int x) { return 13 + (x + 52) * 0.15f; }           /* the fuselage's centre line, tail to nose */
static inline float wkb_r(int x) {                                               /* its half-height: a thin tail, a fat cowl, the spinner's cone */
    if (x < -16) return 4.5f + 6.5f * (x + 52) / 36.0f;
    if (x < 40) return 11;
    return 5.0f * (48 - x) / 8.0f;
}
static inline bool wkb_fuselage(int x, int h) {
    if (x < -52 || x >= 48) return false;
    float d = h - wkb_axis(x), r = wkb_r(x);
    return d > -r && d < r;
}
/* the canopy: a bubble on the spine from x=-6 to 20, a gentle fairing aft, a steep windscreen forward */
static inline float wkb_bump(int x) { return x <= 12 ? 9.0f * (x + 6) / 18.0f : 9.0f * (20 - x) / 8.0f; }
static inline bool wkb_canopy(int x, int h) {
    if (x < -6 || x > 20) return false;
    float top = wkb_axis(x) + 11;
    return h >= top - 1 && h < top + wkb_bump(x);
}
/* the cockpit cut into the fuselage under the canopy, and the torn gap in the rear fuselage */
static inline bool wkb_cut(int x, int h) { float top = wkb_axis(x) + 11; return x >= -2 && x <= 17 && h >= top - 5 && h < top; }
static inline bool wkb_gap(int x, int h) {
    float a = wkb_axis(x), r = wkb_r(x);
    int jag = (int)(chash(x, 9) % 4u), jag2 = (int)(chash(h, 11) % 3u);        /* ragged: the edge wanders per column and row */
    float dx = (x + 31 + (h > a ? jag2 : -jag2)) / 7.0f, dy = (h - a + 1) / (r * 0.55f + jag * 0.6f);
    return dx * dx + dy * dy < 1;
}
/* the near wing: from the root on the fuselage's side down and to the left onto the sand */
#define WKB_WING_ROOT (wkb_axis(6) + 4)
static inline bool wkb_near_wing(int x, int h) {
    float a0 = WKB_WING_ROOT;
    if (h < 0 || h >= a0) return false;
    float u = (a0 - h) / a0;                                                       /* 0 at the root, 1 on the sand */
    return x >= -10 - 26 * u && x <= 24 - 36 * u;
}
/* the far wing: behind the fuselage, up and to the left */
static inline bool wkb_far_wing(int x, int h) {
    float a1 = wkb_axis(6) + 11;
    if (h < a1 || h >= a1 + 20) return false;
    float u = (h - a1) / 20.0f;
    return x >= -4 - 22 * u && x <= 20 - 30 * u;
}
static inline bool wkb_fin(int x, int h) {
    if (h < 20 || h >= 50) return false;
    float t = (h - 20) / 30.0f;
    return x >= -53 + 2 * t && x <= -36 - 12 * t;
}
static inline bool wkb_tailplane(int x, int h) {
    float a = wkb_axis(x), k = a - 3 - h;
    return k >= 0 && k < 6 && x >= -56 + k * 0.5f && x <= -40 - k * 0.3f;
}
/* the propeller: three blades on the hub, up (bent back at the tip), down-left and down-right; the
   blade a pixel is on in *blade (0 up, 1 down-left, 2 down-right) and how far along it in *along */
static inline bool wkb_prop_at(int x, int h, int *blade, int *along) {
    float hub = wkb_axis(42);
    int up = (int)(h - hub);
    if (up >= 0) {
        int bend = up > 16 ? (up - 16) * 3 / 5 : 0;
        *blade = 0; *along = up;
        return up < 27 && x >= 39 - bend && x <= 42 - bend;
    }
    int dn = -up;
    if (dn >= 25 || h < 0) return false;
    *along = dn;
    int lean = dn * 2 / 5;                                                       /* the V: 0.4 px sideways per px down */
    if (x >= 39 - lean && x <= 42 - lean) { *blade = 1; return true; }
    if (x >= 39 + lean && x <= 42 + lean) { *blade = 2; return true; }
    return false;
}
static inline bool wkb_prop(int x, int h) { int b, a; return wkb_prop_at(x, h, &b, &a); }
/* which piece owns a pixel, front to back (the holes count as their piece: no outline inside them) */
static int wkb_piece(int x, int h) {
    if (h < 0) return 0;
    if (wkb_prop(x, h)) return 6;
    if (wkb_near_wing(x, h)) return 2;
    if (wkb_tailplane(x, h)) return 5;
    if (wkb_canopy(x, h)) return 7;
    if (wkb_fuselage(x, h)) return 1;
    if (wkb_fin(x, h)) return 4;
    if (wkb_far_wing(x, h)) return 3;
    return 0;
}
static void draw_wreck_blackwater(ctx_t *c, int cx, bool final, int layer) {
    const uint32_t OUTLINE = 0x0c1216, FRAME = 0xe4e8ea;
    for (int h = -2; h <= WK_H; h++) {
        int y = WK_FY - h;
        if (y < c->oy || y >= c->oy + c->h) continue;
        for (int x = -WK_W / 2; x < WK_W / 2; x++) {
            int X = cx + x;
            if (X < c->ox || X >= c->ox + c->w) continue;
            unsigned noise = ((unsigned)(x + 100) * 1237u + (unsigned)(h + 40) * 719u) ^ ((unsigned)(x + 130) * (unsigned)(h + 17));
            float axis = wkb_axis(x), r = wkb_r(x);
            bool fus = wkb_fuselage(x, h), can = wkb_canopy(x, h);
            uint32_t inside = mix(water_rgb(y), 0x070c10, 0.8f);
            if (layer == 0) { if (fus || can) wk_put(c, X, y, inside, final); continue; }
            int piece = wkb_piece(x, h);
            if (!piece) continue;
            /* the holes: the canopy's glass and the cockpit under it, the torn gap aft */
            bool frame = can && (!wkb_canopy(x - 2, h) || !wkb_canopy(x + 2, h) || !wkb_canopy(x, h + 2) || x == 12 || x == 13);
            bool hole = (piece == 7 && !frame) || (piece == 1 && (wkb_cut(x, h) || wkb_gap(x, h)));
            if (hole) { if (layer < 0) wk_put(c, X, y, inside, final); continue; }
            /* the dark outline: wherever the piece changes */
            if (wkb_piece(x - 1, h) != piece || wkb_piece(x + 1, h) != piece || wkb_piece(x, h - 1) != piece || wkb_piece(x, h + 1) != piece) {
                wk_put(c, X, y, OUTLINE, final);
                continue;
            }
            uint32_t rgb = 0xb4bcc2;
            float t = (h - axis) / (r > 1 ? r : 1);                                /* -1 belly .. 1 spine */
            if (piece == 1) {
                /* the fuselage: bright aluminium above, a shaded belly, panel lines, the cowl's mouth, the spinner */
                rgb = t > 0.55f ? 0xe0e4e6 : t > 0.1f ? 0xbcc4ca : t > -0.45f ? 0x98a2aa : 0x5e6a72;
                if (((x + 52) % 13) == 0 && x < 38) rgb = mix(rgb, 0x3a4248, 0.55f);
                if (h == (int)(axis - 3) && x < 38) rgb = mix(rgb, 0x3a4248, 0.45f);
                if (x >= -21 && x <= -17) rgb = t > 0.4f ? 0xf4d060 : t > -0.45f ? 0xe0b830 : 0x8a7020;   /* the yellow band */
                if (x >= 38 && x < 40 && (t > 0.45f || t < -0.45f)) rgb = 0x1e2428;                     /* the cowl's dark mouth */
                if (x >= 40) rgb = t > 0.2f ? 0xd8dde0 : 0x7a848c;                                      /* the spinner */
                if (x > 44 && x < 48 && (noise & 1)) rgb = mix(rgb, 0xc8441e, 0.6f);                     /* its red nose tip, worn */
                if ((noise % 100) < 5 && t < 0.4f && x < 38) rgb = mix(rgb, 0x8a4a28, 0.6f);            /* rust */
                if (t < -0.6f && (noise % 100) < 28 && x < 36) rgb = (noise & 2) ? 0x4f6a32 : 0x3a5228;  /* growth along the belly */
                /* the lip round the cockpit cut, the torn edge round the gap */
                if (wkb_cut(x - 1, h) || wkb_cut(x + 1, h) || wkb_cut(x, h - 1)) rgb = FRAME;
                if (wkb_gap(x - 1, h) || wkb_gap(x + 1, h) || wkb_gap(x, h - 1) || wkb_gap(x, h + 1)) rgb = (noise % 3) ? 0x2a3036 : FRAME;
            } else if (piece == 7) {
                rgb = FRAME;                                                       /* the canopy frame */
            } else if (piece == 2) {
                /* the near wing: lit along the leading edge, a seam at the root, the big roundel */
                float u = (WKB_WING_ROOT - h) / WKB_WING_ROOT;
                rgb = (noise % 11 == 0) ? 0xc4ccd2 : 0xb4bcc2;
                if (x < -10 - 26 * u + 3) rgb = 0xe0e4e6;                          /* the leading edge in the sun */
                if (x > 24 - 36 * u - 3) rgb = 0x7a848c;                           /* the trailing edge turned from it */
                if (((x + 10 + (int)(26 * u)) % 12) == 6) rgb = mix(rgb, 0x3a4248, 0.4f);
                { int rx = x + 9, rh = (int)((h - 14) * 1.2f); int d2 = rx * rx + rh * rh;   /* the roundel, foreshortened */
                  if (d2 < 90) rgb = 0x2a4fa8;
                  if (d2 < 44) rgb = 0xeef0f0;
                  if (d2 < 14) rgb = 0xd0302a; }
                if (h < 3 && (noise % 100) < 40) rgb = (noise & 2) ? 0x4f6a32 : 0x3a5228;
            } else if (piece == 3) {
                rgb = (noise % 9 == 0) ? 0x9aa4ab : 0x8a949c;                       /* the far wing, in the fuselage's shade */
                if (((x + h) % 10) == 0) rgb = 0x5e6a72;
            } else if (piece == 4) {
                float tt = (h - 20) / 30.0f;
                rgb = (noise % 9 == 0) ? 0xc4ccd2 : 0xb4bcc2;                       /* the fin */
                if (x < -53 + 2 * tt + 6) rgb = 0xd0302a;                            /* the red rudder */
                if (h >= 40 && h < 44) rgb = 0xeef0f0;                               /* the white stripe */
                if (x > -36 - 12 * tt - 3) rgb = 0xe0e4e6;                           /* the lit leading edge */
            } else if (piece == 5) {
                rgb = (h >= axis - 5) ? 0xbcc4ca : 0x7a848c;                         /* the near tailplane */
            } else if (piece == 6) {
                int blade = 0, along = 0; wkb_prop_at(x, h, &blade, &along);
                rgb = along >= (blade ? 19 : 21) ? 0xf0c030 : (noise % 5 == 0) ? 0x4a545c : 0x353b42;   /* the blades, yellow tips */
            }
            if (noise % 31 == 0) rgb = mix(rgb, 0xffffff, 0.12f);
            wk_put(c, X, y, rgb, final);
        }
    }
    draw_floor_mound(c, cx, WRECK_HALF_W - 8, 5, final);
}
