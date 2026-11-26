/* The octopus prototype's draw, for measuring (octo.c). */
#pragma once
#include <stdint.h>

#define OCTO_GW 144              /* the grid's room: the adult with its arms out, jetting */
#define OCTO_GH 96
#define OCTO_NMAX 12
#define OCTO_WT 32
#ifndef OCTO_STAMPS
#define OCTO_STAMPS 1.0f         /* stamps a pixel along an arm (the Python prototype lays 2) */
#endif

typedef struct {
    float sz, m_len, m_b, m_droop, head[2], rest_h, L, w0, w1, taper, curl, eye_r, eye_at[2], far_eye[2], mottle;
    int N;
    float reach[8];
    float wtab[OCTO_WT + 1];
} octo_look_t;
typedef struct { int far; float curl, seg; float P[OCTO_NMAX * 2]; float root[2], a0[2], tip[2], L0, Lmax; } octo_arm_t;
typedef struct { float x, y, theta, rest_theta, squeeze, puff, droop, t, blink, floor; octo_arm_t arm[8];
                 float stream;                    /* 0..1: fleeing, drawn out long: its arms thinner (new fields go LAST: poses.h fills this struct by position) */
                 int eye_set; float eye_at[2], far_eye[2];   /* eye_set: the eyes' places for this pose (it turns side on as it flees), else the look's */
} octo_pose_t;
typedef struct { int x0, y0, w, h, hf, far_n; uint8_t *tone, *part; } octo_grid_t;   /* tone and part: OCTO_GW x OCTO_GH each, WORD ALIGNED (a heap block is; a static array must say so) */

void octo_look_init(octo_look_t *lk);
void octo_bezier(const octo_look_t *lk, float *P, int N, float rx, float ry, float ca, float sa, float tx, float ty, float L0, float Lmax, float floor, int far);
void octo_build(octo_grid_t *g, const octo_look_t *lk, const octo_pose_t *p);
void octo_debug_sack_cache(int on);          /* (tests: the sack's kept shapes on or off) */
extern long g_sack_hits, g_sack_miss;
void octo_palette(uint16_t pal[2][9], const uint8_t base[3], float haze);
void octo_blit(uint16_t *fb, int stride, int W, int H, const octo_grid_t *g, const uint16_t pal[2][9]);
