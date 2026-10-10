/* q4_model.c — see q4_model.h. Derived from karpathy/llama2.c runq.c (MIT),
 * restructured as a library over a memory-mapped 4-bit model. */
#include "q4_model.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

/* On the ESP32-S3 the group dot product runs on the PIE 128-bit unit
 * (16 int8 MACs per ee.vmulas.s8.accx); everywhere else a scalar loop.
 * Both paths dot an UNPACKED int8 weight group against int8 activations —
 * the group is unpacked once and reused across every token of a batch.
 * Define AQUA_PETS_NO_PIE to force the scalar path (e.g. a QEMU build
 * without PIE emulation). Requires gs == 64 and 16-byte aligned buffers. */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(AQUA_PETS_NO_PIE)
#define Q4_PIE 1
static inline int32_t dot_i8_64(const int8_t *w, const int8_t *x) {
    int32_t acc;
    __asm__ volatile(
        "ee.zero.accx\n"
        "ee.vld.128.ip q0, %[w], 16\n"
        "ee.vld.128.ip q1, %[x], 16\n"
        "ee.vld.128.ip q2, %[w], 16\n"
        "ee.vld.128.ip q3, %[x], 16\n"
        "ee.vmulas.s8.accx q0, q1\n"
        "ee.vld.128.ip q0, %[w], 16\n"
        "ee.vld.128.ip q1, %[x], 16\n"
        "ee.vmulas.s8.accx q2, q3\n"
        "ee.vld.128.ip q2, %[w], 16\n"
        "ee.vld.128.ip q3, %[x], 16\n"
        "ee.vmulas.s8.accx q0, q1\n"
        "ee.vmulas.s8.accx q2, q3\n"
        "rur.accx_0 %[acc]\n"
        : [acc] "=r"(acc), [w] "+r"(w), [x] "+r"(x)
        :
        : "memory");
    return acc;
}
/* four tokens against one weight group: the group loads into q0-q3 once and
 * stays there for all four activation rows (stride apart) */
static inline void dot4_i8_64(const int8_t *w, const int8_t *x0, int stride, int32_t iv[4]) {
    const int8_t *x1 = x0 + stride, *x2 = x1 + stride, *x3 = x2 + stride;
    int32_t a0, a1, a2, a3;
    __asm__ volatile(
        "ee.vld.128.ip q0, %[w], 16\n"
        "ee.vld.128.ip q1, %[w], 16\n"
        "ee.vld.128.ip q2, %[w], 16\n"
        "ee.vld.128.ip q3, %[w], 16\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x0], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x0], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x0], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x0], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a0]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x1], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x1], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x1], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x1], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a1]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x2], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x2], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x2], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x2], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a2]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x3], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x3], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x3], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x3], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a3]\n"
        : [a0] "=&r"(a0), [a1] "=&r"(a1), [a2] "=&r"(a2), [a3] "=r"(a3),
          [w] "+r"(w), [x0] "+r"(x0), [x1] "+r"(x1), [x2] "+r"(x2), [x3] "+r"(x3)
        :
        : "memory");
    iv[0] = a0; iv[1] = a1; iv[2] = a2; iv[3] = a3;
}

/* eight tokens per weight-group load: the four activation pointers each hop
 * (4*stride - 64) after their first pass to cover tokens t+4..t+7 */
static inline void dot8_i8_64(const int8_t *w, const int8_t *x0, int stride, int32_t iv[8]) {
    const int8_t *x1 = x0 + stride, *x2 = x1 + stride, *x3 = x2 + stride;
    int hop = 4 * stride - 64;
    int32_t a0, a1, a2, a3, b0, b1, b2, b3;
    __asm__ volatile(
        "ee.vld.128.ip q0, %[w], 16\n"
        "ee.vld.128.ip q1, %[w], 16\n"
        "ee.vld.128.ip q2, %[w], 16\n"
        "ee.vld.128.ip q3, %[w], 16\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x0], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x0], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x0], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x0], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a0]\n"
        "add %[x0], %[x0], %[hop]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x1], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x1], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x1], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x1], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a1]\n"
        "add %[x1], %[x1], %[hop]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x2], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x2], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x2], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x2], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a2]\n"
        "add %[x2], %[x2], %[hop]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x3], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x3], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x3], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x3], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[a3]\n"
        "add %[x3], %[x3], %[hop]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x0], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x0], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x0], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x0], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[b0]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x1], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x1], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x1], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x1], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[b1]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x2], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x2], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x2], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x2], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[b2]\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q4, %[x3], 16\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x3], 16, q0, q4\n"
        "ee.vmulas.s8.accx.ld.ip q4, %[x3], 16, q1, q5\n"
        "ee.vmulas.s8.accx.ld.ip q5, %[x3], 16, q2, q4\n"
        "ee.vmulas.s8.accx q3, q5\n"
        "rur.accx_0 %[b3]\n"
        : [a0] "=&r"(a0), [a1] "=&r"(a1), [a2] "=&r"(a2), [a3] "=&r"(a3),
          [b0] "=&r"(b0), [b1] "=&r"(b1), [b2] "=&r"(b2), [b3] "=r"(b3),
          [w] "+r"(w), [x0] "+r"(x0), [x1] "+r"(x1), [x2] "+r"(x2), [x3] "+r"(x3)
        : [hop] "r"(hop)
        : "memory");
    iv[0] = a0; iv[1] = a1; iv[2] = a2; iv[3] = a3;
    iv[4] = b0; iv[5] = b1; iv[6] = b2; iv[7] = b3;
}

/* single token, PACKED weights: unpack the group's nibbles vectorially
 * (mask for lows = evens block, 32-bit-lane shift + mask for highs = odds
 * block, matching the split activation layout) and dot in one pass. */
static const int8_t q4pk_mask[16] __attribute__((aligned(16))) =
    { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15 };
static const int8_t q4pk_eight[16] __attribute__((aligned(16))) =
    { 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 };
static inline int32_t dot_q4pk_64(const uint8_t *wp, const int8_t *x) {
    int32_t acc;
    const int8_t *mask = q4pk_mask, *eight = q4pk_eight;
    /* weight tensors in the model file are only 8-byte aligned (the embedding
       scale block is 648 bytes), so the 32 packed bytes are fetched with
       aligned loads + a SAR_BYTE funnel shift. The 16-byte overread stays
       inside the 8 MB mapped model partition. */
    const uint8_t *wa = (const uint8_t *)((uintptr_t)wp & ~(uintptr_t)15);
    uint32_t shift = (uint32_t)((uintptr_t)wp & 15);
    __asm__ volatile(
        "ssai 4\n"
        "wur.sar_byte %[sh]\n"
        "ee.vld.128.ip q6, %[mask], 0\n"
        "ee.vld.128.ip q7, %[eight], 0\n"
        "ee.vld.128.ip q0, %[w], 16\n"
        "ee.vld.128.ip q1, %[w], 16\n"
        "ee.vld.128.ip q2, %[w], 0\n"
        "ee.src.q q0, q0, q1\n"
        "ee.src.q q1, q1, q2\n"
        "ee.andq q2, q0, q6\n"
        "ee.andq q3, q1, q6\n"
        "ee.vsubs.s8 q2, q2, q7\n"
        "ee.vsubs.s8 q3, q3, q7\n"
        "ee.vsr.32 q4, q0\n"
        "ee.vsr.32 q5, q1\n"
        "ee.andq q4, q4, q6\n"
        "ee.andq q5, q5, q6\n"
        "ee.vsubs.s8 q4, q4, q7\n"
        "ee.vsubs.s8 q5, q5, q7\n"
        "ee.zero.accx\n"
        "ee.vld.128.ip q0, %[x], 16\n"
        "ee.vmulas.s8.accx.ld.ip q1, %[x], 16, q2, q0\n"
        "ee.vmulas.s8.accx.ld.ip q0, %[x], 16, q3, q1\n"
        "ee.vmulas.s8.accx.ld.ip q1, %[x], 16, q4, q0\n"
        "ee.vmulas.s8.accx q5, q1\n"
        "rur.accx_0 %[acc]\n"
        : [acc] "=r"(acc), [w] "+r"(wa), [x] "+r"(x), [mask] "+r"(mask), [eight] "+r"(eight)
        : [sh] "r"(shift)
        : "memory");
    return acc;
}
#define Q4_PK 1
#else
#define Q4_PK 0
#define Q4_PIE 0
static inline int32_t dot_i8_64(const int8_t *w, const int8_t *x) {
    int32_t acc = 0;
    for (int k = 0; k < 64; k++) acc += (int32_t)w[k] * x[k];
    return acc;
}
#endif

int64_t (*q4_clock_us)(void) = NULL;
int64_t q4_prof_us[6];
#define QPROF_MARK() (q4_clock_us ? q4_clock_us() : 0)
#define QPROF_ADD(i, t0) do { if (q4_clock_us) { int64_t _n = q4_clock_us(); q4_prof_us[i] += _n - (t0); (t0) = _n; } } while (0)

/* Activations and unpacked weights use a SPLIT group layout: within each
 * 64-element group, even-indexed elements sit at [0..31] and odd-indexed at
 * [32..63]. Packed q4 byte j holds elements 2j (low nibble) and 2j+1 (high),
 * so the split makes the vector nibble-unpack line up: low nibbles are the
 * evens block, high nibbles the odds block. Dot products are order-agnostic
 * as long as both operands agree. */
static inline void unpack_group(int8_t *wg, const uint8_t *wq, int gs) {
    int half = gs >> 1;
    for (int j = 0; j < half; j++) {
        uint8_t b = wq[j];
        wg[j] = (int8_t)((b & 0x0f) - 8); wg[half + j] = (int8_t)((b >> 4) - 8);
    }
}

typedef struct { int8_t *q; float *s; } qact_t;           /* int8 activations */
typedef struct { const uint8_t *q; const uint16_t *s; } q4w_t;  /* 4-bit weights in flash */

struct q4_model {
    q4_config_t c;
    int gs;
    const float *rms_att, *rms_ffn, *rms_final;
    q4w_t emb, *wq, *wk, *wv, *wo, *w1, *w2, *w3, wcls;
    /* run state */
    float *x, *xb, *xb2, *hb, *hb2, *q, *att, *logits, *key_cache, *value_cache;
    qact_t xq, hq;
    /* batched-prefill state (allocated on first use): per-token activations */
    int    bmax;
    float *bx, *bxb, *bq, *bhb, *bhb2;      /* [bmax][dim] / [bmax][hidden] */
    int8_t *bxq_q; float *bxq_s;            /* [bmax][dim], [bmax][dim/gs] */
    int8_t *bhq_q; float *bhq_s;            /* [bmax][hidden], [bmax][hidden/gs] */
    float *btile;                           /* [bmax][Q4_TILE_D] fast-mem out tile */
    float *rope_cs;                         /* [seq][head_size/2] {cos,sin} table */
    void *(*alloc)(size_t);
};

/* fast expf (exp2 split + 5th-order minimax on the fraction, rel err ~2e-7):
 * libm expf is ~400 software cycles on Xtensa; the softmax/swiglu inner loops
 * call it hundreds of thousands of times per decision. */
static inline float fast_expf(float x) {
    float t = x * 1.4426950408889634f;             /* x / ln2 */
    if (t <= -126.0f) return 0.0f;
    if (t >= 127.0f) t = 127.0f;
    int ti = (int)t; if (t < ti) ti--;             /* floor */
    float f = t - ti;
    float p = 1.0f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f
                  + f * (0.00961804f + f * 0.00133990f))));
    union { uint32_t u; float fl; } v; v.u = (uint32_t)(ti + 127) << 23;
    return v.fl * p;
}

static inline float half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000) << 16, exp = (h >> 10) & 0x1f, man = h & 0x3ff, f;
    if (exp == 0) { if (man == 0) f = sign; else { exp = 113; while (!(man & 0x400)) { man <<= 1; exp--; } man &= 0x3ff; f = sign | (exp << 23) | (man << 13); } }
    else if (exp == 31) f = sign | 0x7f800000 | (man << 13);
    else f = sign | ((exp + 112) << 23) | (man << 13);
    float out; memcpy(&out, &f, 4); return out;
}

void *(*q4_fast_alloc)(size_t) = NULL;

/* run-state buffers must be 16-byte aligned for the PIE loads (allocations
 * are permanent, so the rounded-up base pointer is simply dropped) */
static void *alloc16(q4_model_t *m, size_t n) {
    uint8_t *p = m->alloc(n + 15);
    return p ? (void *)(((uintptr_t)p + 15) & ~(uintptr_t)15) : NULL;
}

/* hot-path buffers: fast memory when the platform provides it */
static void *alloc16_fast(q4_model_t *m, size_t n) {
    if (q4_fast_alloc) {
        uint8_t *p = q4_fast_alloc(n + 15);
        if (p) return (void *)(((uintptr_t)p + 15) & ~(uintptr_t)15);
    }
    return alloc16(m, n);
}

static void map_q4(q4w_t *t, const uint8_t **p, int n, int numel, int gs) {
    for (int i = 0; i < n; i++) {
        t[i].q = *p; *p += numel / 2;
        t[i].s = (const uint16_t *)*p; *p += (numel / gs) * 2;
    }
}

static bool size_mul(size_t a, size_t b, size_t *out) {
    if (a && b > SIZE_MAX / a) return false;
    *out = a * b; return true;
}

static bool size_add(size_t *total, size_t add) {
    if (add > SIZE_MAX - *total) return false;
    *total += add; return true;
}

static bool add_f32(size_t *total, size_t count) {
    size_t bytes;
    return size_mul(count, sizeof(float), &bytes) && size_add(total, bytes);
}

static bool add_q4(size_t *total, size_t tensors, size_t numel, size_t gs) {
    if (!gs || numel % 2 || numel % gs) return false;
    size_t one, all;
    if (!size_mul(numel / gs, sizeof(uint16_t), &one) ||
        !size_add(&one, numel / 2) ||
        !size_mul(tensors, one, &all)) return false;
    return size_add(total, all);
}

static bool model_layout_valid(const q4_config_t *c, int gs, bool shared, size_t len) {
    if (gs != 64 || c->dim <= 0 || c->hidden_dim <= 0 || c->n_layers <= 0 ||
        c->n_heads <= 0 || c->n_kv_heads <= 0 || c->vocab_size < 3 || c->seq_len < 2)
        return false;
    if (c->n_kv_heads > c->n_heads || c->n_heads % c->n_kv_heads ||
        c->dim % c->n_heads || (c->dim / c->n_heads) % 2 ||
        c->dim % gs || c->hidden_dim % gs)
        return false;
    if (c->dim > INT_MAX / c->dim || c->dim > INT_MAX / c->hidden_dim ||
        c->vocab_size > INT_MAX / c->dim)
        return false;
    int kv_dim = (c->dim / c->n_heads) * c->n_kv_heads;
    if ((size_t)c->n_layers * c->seq_len * kv_dim > INT_MAX ||
        (size_t)c->n_heads * c->seq_len > INT_MAX)
        return false;

    size_t need = 256;
    return add_f32(&need, (size_t)c->n_layers * c->dim) &&
           add_f32(&need, (size_t)c->n_layers * c->dim) &&
           add_f32(&need, c->dim) &&
           add_q4(&need, 1, (size_t)c->vocab_size * c->dim, gs) &&
           add_q4(&need, c->n_layers, (size_t)c->dim * c->dim, gs) &&
           add_q4(&need, c->n_layers, (size_t)c->dim * kv_dim, gs) &&
           add_q4(&need, c->n_layers, (size_t)c->dim * kv_dim, gs) &&
           add_q4(&need, c->n_layers, (size_t)c->dim * c->dim, gs) &&
           add_q4(&need, c->n_layers, (size_t)c->dim * c->hidden_dim, gs) &&
           add_q4(&need, c->n_layers, (size_t)c->hidden_dim * c->dim, gs) &&
           add_q4(&need, c->n_layers, (size_t)c->dim * c->hidden_dim, gs) &&
           (shared || add_q4(&need, 1, (size_t)c->dim * c->vocab_size, gs)) &&
           need <= len;
}

/* boot self-check: the (possibly SIMD) dot kernels must agree with plain C */
static bool dot_selfcheck(void) {
    int8_t a[64] __attribute__((aligned(16))), b[64] __attribute__((aligned(16)));
    int32_t want = 0;
    for (int i = 0; i < 64; i++) {
        a[i] = (int8_t)((i * 37 + 11) % 15 - 8);         /* q4 range [-8,7] */
        b[i] = (int8_t)((i * 73 + 5) % 255 - 127);       /* int8 activation */
        want += (int32_t)a[i] * b[i];
    }
    if (dot_i8_64(a, b) != want) return false;
#if Q4_PK
    /* packed-nibble kernel vs unpack_group + dot — at every alignment phase
       the model file can produce (tensors are only 8-byte aligned) */
    uint8_t pkbuf[64] __attribute__((aligned(16)));
    int8_t wg[64] __attribute__((aligned(16)));
    for (int off = 0; off <= 15; off += 8) {
        uint8_t *pk = pkbuf + off;
        for (int j = 0; j < 32; j++) pk[j] = (uint8_t)(j * 41 + 17 + off);
        unpack_group(wg, pk, 64);
        if (dot_q4pk_64(pk, b) != dot_i8_64(wg, b)) return false;
    }
#endif
    return true;
}

q4_model_t *q4_model_open(const uint8_t *bin, size_t len, void *(*alloc)(size_t)) {
    if (!bin || !alloc || len < 256 || !dot_selfcheck()) return NULL;
    uint32_t magic; memcpy(&magic, bin, 4);
    int version; memcpy(&version, bin + 4, 4);
    if (magic != 0x616b3432 || version != 3) return NULL;
    q4_config_t cfg; memcpy(&cfg, bin + 8, sizeof cfg);
    uint8_t shared = bin[8 + 28];
    int gs; memcpy(&gs, bin + 8 + 28 + 1, 4);
    if (shared > 1 || !model_layout_valid(&cfg, gs, shared != 0, len)) return NULL;
    q4_model_t *m = alloc(sizeof *m); if (!m) return NULL; memset(m, 0, sizeof *m);
    m->alloc = alloc; m->c = cfg; m->gs = gs;
    const q4_config_t *c = &m->c;
    const uint8_t *p = bin + 256;
    m->rms_att = (const float *)p; p += (size_t)c->n_layers * c->dim * 4;
    m->rms_ffn = (const float *)p; p += (size_t)c->n_layers * c->dim * 4;
    m->rms_final = (const float *)p; p += (size_t)c->dim * 4;
    int kv_dim = (c->dim / c->n_heads) * c->n_kv_heads;
    map_q4(&m->emb, &p, 1, c->vocab_size * c->dim, m->gs);
    m->wq = alloc((size_t)c->n_layers * sizeof(q4w_t));
    m->wk = alloc((size_t)c->n_layers * sizeof(q4w_t));
    m->wv = alloc((size_t)c->n_layers * sizeof(q4w_t));
    m->wo = alloc((size_t)c->n_layers * sizeof(q4w_t));
    m->w1 = alloc((size_t)c->n_layers * sizeof(q4w_t));
    m->w2 = alloc((size_t)c->n_layers * sizeof(q4w_t));
    m->w3 = alloc((size_t)c->n_layers * sizeof(q4w_t));
    if (!m->wq || !m->wk || !m->wv || !m->wo || !m->w1 || !m->w2 || !m->w3) return NULL;
    map_q4(m->wq, &p, c->n_layers, c->dim * c->dim, m->gs);
    map_q4(m->wk, &p, c->n_layers, c->dim * kv_dim, m->gs);
    map_q4(m->wv, &p, c->n_layers, c->dim * kv_dim, m->gs);
    map_q4(m->wo, &p, c->n_layers, c->dim * c->dim, m->gs);
    map_q4(m->w1, &p, c->n_layers, c->dim * c->hidden_dim, m->gs);
    map_q4(m->w2, &p, c->n_layers, c->hidden_dim * c->dim, m->gs);
    map_q4(m->w3, &p, c->n_layers, c->dim * c->hidden_dim, m->gs);
    if (shared) m->wcls = m->emb; else map_q4(&m->wcls, &p, 1, c->dim * c->vocab_size, m->gs);
    /* run state */
    m->x = alloc((size_t)c->dim * 4); m->xb = alloc((size_t)c->dim * 4); m->xb2 = alloc((size_t)c->dim * 4);
    m->hb = alloc((size_t)c->hidden_dim * 4); m->hb2 = alloc((size_t)c->hidden_dim * 4);
    m->q = alloc((size_t)c->dim * 4); m->att = alloc((size_t)c->n_heads * c->seq_len * 4);
    m->logits = alloc((size_t)c->vocab_size * 4);
    m->key_cache = alloc((size_t)c->n_layers * c->seq_len * kv_dim * 4);
    m->value_cache = alloc((size_t)c->n_layers * c->seq_len * kv_dim * 4);
    m->xq.q = alloc16_fast(m, c->dim); m->xq.s = alloc16_fast(m, c->dim / m->gs * 4);
    m->hq.q = alloc16_fast(m, c->hidden_dim); m->hq.s = alloc16_fast(m, c->hidden_dim / m->gs * 4);
    if (!m->x || !m->xb || !m->xb2 || !m->hb || !m->hb2 || !m->q || !m->att || !m->logits ||
        !m->key_cache || !m->value_cache || !m->xq.q || !m->xq.s || !m->hq.q || !m->hq.s)
        return NULL;   /* not enough RAM (e.g. no PSRAM): caller falls back to rules */
    /* RoPE cos/sin table: powf/sinf per pair per token is libm-call soup on
       device; the whole table is seq * head_size/2 pairs. */
    {
        int hs = c->dim / c->n_heads, hs2 = hs / 2;
        m->rope_cs = alloc16_fast(m, (size_t)c->seq_len * hs2 * 2 * sizeof(float));
        if (!m->rope_cs) return NULL;
        for (int pos = 0; pos < c->seq_len; pos++)
            for (int h2 = 0; h2 < hs2; h2++) {
                float freq = 1.0f / powf(10000.0f, (2.0f * h2) / (float)hs);
                m->rope_cs[(pos * hs2 + h2) * 2]     = cosf(pos * freq);
                m->rope_cs[(pos * hs2 + h2) * 2 + 1] = sinf(pos * freq);
            }
    }
    return m;
}

const q4_config_t *q4_model_config(const q4_model_t *m) { return &m->c; }

static void rmsnorm(float *o, const float *x, const float *w, int n) {
    float ss = 0; for (int j = 0; j < n; j++) ss += x[j] * x[j];
    ss = 1.0f / sqrtf(ss / n + 1e-5f);
    for (int j = 0; j < n; j++) o[j] = w[j] * (ss * x[j]);
}
static void softmax(float *x, int n) {
    float mx = x[0]; for (int i = 1; i < n; i++) if (x[i] > mx) mx = x[i];
    float s = 0; for (int i = 0; i < n; i++) { x[i] = fast_expf(x[i] - mx); s += x[i]; }
    for (int i = 0; i < n; i++) x[i] /= s;
}
static inline int fast_round(float v) {           /* lrintf is a libm call */
    return v >= 0 ? (int)(v + 0.5f) : -(int)(0.5f - v);
}
/* int8-quantize into the SPLIT group layout (see unpack_group) */
static void quantize(qact_t *qx, const float *x, int n, int gs) {
    int half = gs >> 1;
    for (int g = 0; g < n / gs; g++) {
        float wmax = 0; for (int i = 0; i < gs; i++) { float v = fabsf(x[g * gs + i]); if (v > wmax) wmax = v; }
        float scale = wmax / 127.0f; qx->s[g] = scale;
        float inv = scale > 0 ? 1.0f / scale : 0;
        int8_t *q = qx->q + g * gs;
        for (int j = 0; j < half; j++) {
            q[j]        = (int8_t)fast_round(x[g * gs + 2 * j] * inv);
            q[half + j] = (int8_t)fast_round(x[g * gs + 2 * j + 1] * inv);
        }
    }
}
/* xout[d] = W[d,n] @ x[n]; the hot loop — int8 activations x 4-bit weights.
 * Each weight group is unpacked once and dotted on the PIE unit (scalar
 * elsewhere). gs must be 64 (asserted at open). */
static void matmul(float *xout, const qact_t *x, const q4w_t *w, int n, int d, int gs) {
#if !Q4_PK
    int8_t wg[64] __attribute__((aligned(16)));
#endif
    for (int i = 0; i < d; i++) {
        float val = 0; int in = i * n;
        for (int j = 0; j <= n - gs; j += gs) {
#if Q4_PK
            val += (float)dot_q4pk_64(w->q + ((in + j) >> 1), x->q + j)
                   * half_to_float(w->s[(in + j) / gs]) * x->s[j / gs];
#else
            unpack_group(wg, w->q + ((in + j) >> 1), gs);
            val += (float)dot_i8_64(wg, x->q + j)
                   * half_to_float(w->s[(in + j) / gs]) * x->s[j / gs];
#endif
        }
        xout[i] = val;
    }
}

float *q4_model_forward(q4_model_t *m, int token, int pos) {
    if (!m || token < 0 || token >= m->c.vocab_size || pos < 0 || pos >= m->c.seq_len) return NULL;
    int64_t f0 = QPROF_MARK();
    const q4_config_t *c = &m->c; int gs = m->gs;
    int dim = c->dim, kv_dim = (dim / c->n_heads) * c->n_kv_heads, kv_mul = c->n_heads / c->n_kv_heads;
    int hidden = c->hidden_dim, head_size = dim / c->n_heads;
    float *x = m->x;
    /* embedding row (dequantize) */
    for (int i = 0; i < dim; i++) {
        int idx = token * dim + i;
        int q = (int)((idx & 1) ? (m->emb.q[idx >> 1] >> 4) : (m->emb.q[idx >> 1] & 0x0f)) - 8;
        x[i] = q * half_to_float(m->emb.s[idx / gs]);
    }
    for (int l = 0; l < c->n_layers; l++) {
        rmsnorm(m->xb, x, m->rms_att + l * dim, dim);
        quantize(&m->xq, m->xb, dim, gs);
        int loff = l * c->seq_len * kv_dim;
        float *k = m->key_cache + loff + pos * kv_dim, *v = m->value_cache + loff + pos * kv_dim;
        matmul(m->q, &m->xq, &m->wq[l], dim, dim, gs);
        matmul(k, &m->xq, &m->wk[l], dim, kv_dim, gs);
        matmul(v, &m->xq, &m->wv[l], dim, kv_dim, gs);
        /* RoPE (precomputed table) */
        for (int i = 0; i < dim; i += 2) {
            int hd = i % head_size;
            const float *cs = m->rope_cs + ((size_t)pos * (head_size / 2) + hd / 2) * 2;
            float fcr = cs[0], fci = cs[1];
            int rotn = i < kv_dim ? 2 : 1;
            for (int vv = 0; vv < rotn; vv++) {
                float *vec = vv == 0 ? m->q : k;
                float v0 = vec[i], v1 = vec[i + 1];
                vec[i] = v0 * fcr - v1 * fci; vec[i + 1] = v0 * fci + v1 * fcr;
            }
        }
        /* attention */
        for (int h = 0; h < c->n_heads; h++) {
            float *q = m->q + h * head_size, *att = m->att + h * c->seq_len;
            for (int t = 0; t <= pos; t++) {
                float *kk = m->key_cache + loff + t * kv_dim + (h / kv_mul) * head_size, sc = 0;
                for (int i = 0; i < head_size; i++) sc += q[i] * kk[i];
                att[t] = sc / sqrtf((float)head_size);
            }
            softmax(att, pos + 1);
            float *xb = m->xb + h * head_size; memset(xb, 0, head_size * 4);
            for (int t = 0; t <= pos; t++) {
                float *vv = m->value_cache + loff + t * kv_dim + (h / kv_mul) * head_size, a = att[t];
                for (int i = 0; i < head_size; i++) xb[i] += a * vv[i];
            }
        }
        quantize(&m->xq, m->xb, dim, gs);
        matmul(m->xb2, &m->xq, &m->wo[l], dim, dim, gs);
        for (int i = 0; i < dim; i++) x[i] += m->xb2[i];
        /* ffn */
        rmsnorm(m->xb, x, m->rms_ffn + l * dim, dim);
        quantize(&m->xq, m->xb, dim, gs);
        matmul(m->hb, &m->xq, &m->w1[l], dim, hidden, gs);
        matmul(m->hb2, &m->xq, &m->w3[l], dim, hidden, gs);
        for (int i = 0; i < hidden; i++) { float val = m->hb[i]; val *= 1.0f / (1.0f + fast_expf(-val)); m->hb[i] = val * m->hb2[i]; }
        quantize(&m->hq, m->hb, hidden, gs);
        matmul(m->xb, &m->hq, &m->w2[l], hidden, dim, gs);
        for (int i = 0; i < dim; i++) x[i] += m->xb[i];
    }
    rmsnorm(x, x, m->rms_final, dim);
    quantize(&m->xq, x, dim, gs);
    matmul(m->logits, &m->xq, &m->wcls, dim, c->vocab_size, gs);
    QPROF_ADD(5, f0);
    return m->logits;
}


/* ---- batched prefill: weights are read ONCE per layer for all n tokens ----
 * out[t*d + i] = W[i,:] . X_t  for t in [0,n). Each weight group's nibbles are
 * unpacked once and dotted against every token's int8 activations. This is the
 * on-device latency lever: a 44-token prompt costs ~1 weight pass, not 44. */
/* out rows are written through a fast-memory tile: out[t*d + i] has a multi-KB
 * stride between tokens, which folds all the row's lines into two cache sets
 * and turns the write stream into constant conflict misses. The tile absorbs
 * Q4_TILE_D rows and flushes each destination line exactly once. */
#define Q4_TILE_D 32
static void matmul_batch(float *out, const int8_t *xq, const float *xs, int n_tok,
                         const q4w_t *w, int n, int d, int gs, float *tile) {
    int8_t wg[64] __attribute__((aligned(16)));   /* unpacked group */
    float acc[64];                                /* per-token row accumulator (n_tok < seq 64) */
    int ngroups = n / gs, sg = n / gs;            /* scale stride per token */
    for (int i0 = 0; i0 < d; i0 += Q4_TILE_D) {
        int td = d - i0 < Q4_TILE_D ? d - i0 : Q4_TILE_D;
        for (int ii = 0; ii < td; ii++) {
            int in = (i0 + ii) * n;
            for (int t = 0; t < n_tok; t++) acc[t] = 0;
            for (int g = 0; g < ngroups; g++) {
                unpack_group(wg, w->q + ((in + g * gs) >> 1), gs);
                float ws = half_to_float(w->s[(in + g * gs) / gs]);
                const int8_t *x = xq + g * gs;
                int t = 0;
#if Q4_PIE
                for (; t + 8 <= n_tok; t += 8, x += 8 * n) {
                    int32_t iv[8];
                    dot8_i8_64(wg, x, n, iv);
                    for (int k = 0; k < 8; k++)
                        acc[t + k] += (float)iv[k] * ws * xs[(t + k) * sg + g];
                }
                for (; t + 4 <= n_tok; t += 4, x += 4 * n) {
                    int32_t iv[4];
                    dot4_i8_64(wg, x, n, iv);
                    for (int k = 0; k < 4; k++)
                        acc[t + k] += (float)iv[k] * ws * xs[(t + k) * sg + g];
                }
#endif
                for (; t < n_tok; t++, x += n)
                    acc[t] += (float)dot_i8_64(wg, x) * ws * xs[t * sg + g];
            }
            for (int t = 0; t < n_tok; t++) tile[t * Q4_TILE_D + ii] = acc[t];
        }
        for (int t = 0; t < n_tok; t++)
            memcpy(out + t * d + i0, tile + t * Q4_TILE_D, td * 4);
    }
}

static bool ensure_batch(q4_model_t *m, int n) {
    if (m->bmax >= n) return true;
    const q4_config_t *c = &m->c; int gs = m->gs;
    m->bmax = n;
    m->bx   = m->alloc((size_t)n * c->dim * 4);
    m->bxb  = m->alloc((size_t)n * c->dim * 4);
    m->bq   = m->alloc((size_t)n * c->dim * 4);
    m->bhb  = m->alloc((size_t)n * c->hidden_dim * 4);
    m->bhb2 = m->alloc((size_t)n * c->hidden_dim * 4);
    m->bxq_q = alloc16_fast(m, (size_t)n * c->dim); m->bxq_s = alloc16_fast(m, (size_t)n * (c->dim / gs) * 4);
    m->bhq_q = alloc16_fast(m, (size_t)n * c->hidden_dim); m->bhq_s = alloc16_fast(m, (size_t)n * (c->hidden_dim / gs) * 4);
    m->btile = alloc16_fast(m, (size_t)n * Q4_TILE_D * 4);
    return m->bx && m->bxb && m->bq && m->bhb && m->bhb2 && m->bxq_q && m->bxq_s && m->bhq_q && m->bhq_s && m->btile;
}

float *q4_model_prefill(q4_model_t *m, const int *toks, int n) {
    if (!m || !toks) return NULL;
    const q4_config_t *c = &m->c; int gs = m->gs;
    int dim = c->dim, kv_dim = (dim / c->n_heads) * c->n_kv_heads, kv_mul = c->n_heads / c->n_kv_heads;
    int hidden = c->hidden_dim, head_size = dim / c->n_heads;
    if (n <= 0 || n > c->seq_len - 1 || !ensure_batch(m, n)) return NULL;
    for (int t = 0; t < n; t++) if (toks[t] < 0 || toks[t] >= c->vocab_size) return NULL;
    /* embeddings */
    for (int t = 0; t < n; t++)
        for (int i = 0; i < dim; i++) {
            int idx = toks[t] * dim + i;
            int q = (int)((idx & 1) ? (m->emb.q[idx >> 1] >> 4) : (m->emb.q[idx >> 1] & 0x0f)) - 8;
            m->bx[t * dim + i] = q * half_to_float(m->emb.s[idx / gs]);
        }
    qact_t qa;
    int64_t p0 = QPROF_MARK();
    for (int l = 0; l < c->n_layers; l++) {
        int loff = l * c->seq_len * kv_dim;
        for (int t = 0; t < n; t++) {
            rmsnorm(m->bxb + t * dim, m->bx + t * dim, m->rms_att + l * dim, dim);
            qa.q = m->bxq_q + t * dim; qa.s = m->bxq_s + t * (dim / gs);
            quantize(&qa, m->bxb + t * dim, dim, gs);
        }
        QPROF_ADD(2, p0);
        /* q for all tokens; k,v straight into the cache rows */
        matmul_batch(m->bq, m->bxq_q, m->bxq_s, n, &m->wq[l], dim, dim, gs, m->btile);
        /* k/v: out must be contiguous per token at stride d=kv_dim -> cache rows are exactly that */
        matmul_batch(m->key_cache + loff,   m->bxq_q, m->bxq_s, n, &m->wk[l], dim, kv_dim, gs, m->btile);
        matmul_batch(m->value_cache + loff, m->bxq_q, m->bxq_s, n, &m->wv[l], dim, kv_dim, gs, m->btile);
        QPROF_ADD(0, p0);
        for (int t = 0; t < n; t++) {
            float *q = m->bq + t * dim, *k = m->key_cache + loff + t * kv_dim;
            for (int i = 0; i < dim; i += 2) {
                int hd = i % head_size;
                const float *cs = m->rope_cs + ((size_t)t * (head_size / 2) + hd / 2) * 2;
                float fcr = cs[0], fci = cs[1];
                int rotn = i < kv_dim ? 2 : 1;
                for (int vv = 0; vv < rotn; vv++) {
                    float *vec = vv == 0 ? q : k;
                    float v0 = vec[i], v1 = vec[i + 1];
                    vec[i] = v0 * fcr - v1 * fci; vec[i + 1] = v0 * fci + v1 * fcr;
                }
            }
            QPROF_ADD(3, p0);
            /* causal attention for token t */
            for (int h = 0; h < c->n_heads; h++) {
                float *qh = q + h * head_size, *att = m->att + h * c->seq_len;
                for (int p = 0; p <= t; p++) {
                    float *kk = m->key_cache + loff + p * kv_dim + (h / kv_mul) * head_size, sc = 0;
                    for (int i = 0; i < head_size; i++) sc += qh[i] * kk[i];
                    att[p] = sc / sqrtf((float)head_size);
                }
                softmax(att, t + 1);
                float *xb = m->bxb + t * dim + h * head_size; memset(xb, 0, head_size * 4);
                for (int p = 0; p <= t; p++) {
                    float *vv = m->value_cache + loff + p * kv_dim + (h / kv_mul) * head_size, a = att[p];
                    for (int i = 0; i < head_size; i++) xb[i] += a * vv[i];
                }
            }
            qa.q = m->bxq_q + t * dim; qa.s = m->bxq_s + t * (dim / gs);
            quantize(&qa, m->bxb + t * dim, dim, gs);
            QPROF_ADD(1, p0);
        }
        matmul_batch(m->bq, m->bxq_q, m->bxq_s, n, &m->wo[l], dim, dim, gs, m->btile);   /* reuse bq as xb2 */
        QPROF_ADD(0, p0);
        for (int t = 0; t < n; t++) {
            for (int i = 0; i < dim; i++) m->bx[t * dim + i] += m->bq[t * dim + i];
            rmsnorm(m->bxb + t * dim, m->bx + t * dim, m->rms_ffn + l * dim, dim);
            qa.q = m->bxq_q + t * dim; qa.s = m->bxq_s + t * (dim / gs);
            quantize(&qa, m->bxb + t * dim, dim, gs);
        }
        QPROF_ADD(2, p0);
        matmul_batch(m->bhb,  m->bxq_q, m->bxq_s, n, &m->w1[l], dim, hidden, gs, m->btile);
        matmul_batch(m->bhb2, m->bxq_q, m->bxq_s, n, &m->w3[l], dim, hidden, gs, m->btile);
        QPROF_ADD(0, p0);
        for (int t = 0; t < n; t++) {
            float *hb = m->bhb + t * hidden, *hb2 = m->bhb2 + t * hidden;
            for (int i = 0; i < hidden; i++) { float v = hb[i]; v *= 1.0f / (1.0f + fast_expf(-v)); hb[i] = v * hb2[i]; }
            qa.q = m->bhq_q + t * hidden; qa.s = m->bhq_s + t * (hidden / gs);
            quantize(&qa, hb, hidden, gs);
        }
        QPROF_ADD(4, p0);
        matmul_batch(m->bxb, m->bhq_q, m->bhq_s, n, &m->w2[l], hidden, dim, gs, m->btile);
        for (int t = 0; t < n; t++)
            for (int i = 0; i < dim; i++) m->bx[t * dim + i] += m->bxb[t * dim + i];
        QPROF_ADD(0, p0);
    }
    /* logits for the LAST token only; leave m->x holding it like forward() would */
    memcpy(m->x, m->bx + (n - 1) * dim, dim * 4);
    rmsnorm(m->x, m->x, m->rms_final, dim);
    quantize(&m->xq, m->x, dim, gs);
    matmul(m->logits, &m->xq, &m->wcls, dim, c->vocab_size, gs);
    return m->logits;
}

int64_t q4_model_bench(q4_model_t *m, int which, int n_tok, int64_t (*clock_us)(void)) {
    const q4_config_t *c = &m->c; int gs = m->gs;
    static q4w_t ram_w;                            /* case 2/4: weights copied out of flash */
    if (!ensure_batch(m, n_tok)) return -1;
    size_t qbytes = (size_t)c->dim * c->hidden_dim / 2;
    size_t sbytes = (size_t)c->dim * c->hidden_dim / gs * 2;
    if ((which == 2 || which == 4) && !ram_w.q) {
        uint8_t *q = alloc16(m, qbytes); uint16_t *s = m->alloc(sbytes);
        if (!q || !s) return -1;
        memcpy(q, m->w1[0].q, qbytes); memcpy(s, (void *)m->w1[0].s, sbytes);
        ram_w.q = q; ram_w.s = s;
    }
    memset(m->bxq_q, 1, (size_t)n_tok * c->dim);
    for (int i = 0; i < n_tok * c->dim / gs; i++) m->bxq_s[i] = 1.0f;
    memset(m->xq.q, 1, c->dim);
    for (int i = 0; i < c->dim / gs; i++) m->xq.s[i] = 1.0f;
    const q4w_t *w = (which == 2 || which == 4) ? &ram_w : &m->w1[0];
    int64_t t0 = clock_us();
    switch (which) {
    case 0: {
        int8_t a[64] __attribute__((aligned(16))); memset(a, 3, sizeof a);
        volatile int32_t sink = 0;
        for (int i = 0; i < 100000; i++) sink += dot_i8_64(a, m->bxq_q + (i & 1023) * 64);
        (void)sink; break;
    }
    case 1: case 2: matmul_batch(m->bhb, m->bxq_q, m->bxq_s, n_tok, w, c->dim, c->hidden_dim, gs, m->btile); break;
    case 3: case 4: matmul(m->hb, &m->xq, w, c->dim, c->hidden_dim, gs); break;
    default: return -1;
    }
    return clock_us() - t0;
}

static int argmax(const float *l, int v) { int b = 0; for (int i = 1; i < v; i++) if (l[i] > l[b]) b = i; return b; }

static bool token_ids_valid(const q4_model_t *m, const int *ids, int n) {
    if (!m || !ids || n <= 0) return false;
    for (int i = 0; i < n; i++) if (ids[i] < 0 || ids[i] >= m->c.vocab_size) return false;
    return true;
}

int q4_model_generate(q4_model_t *m, const int *prompt, int n_prompt, int *out, int max_out) {
    if (!out || max_out < 0 || !token_ids_valid(m, prompt, n_prompt)) return 0;
    int n = 0;
    float *logits = q4_model_prefill(m, prompt, n_prompt);      /* one pass over the weights */
    if (!logits) {                                                /* fallback: token by token */
        for (int p = 0; p < n_prompt; p++) {
            logits = q4_model_forward(m, prompt[p], p);
            if (!logits) return 0;
        }
    }
    int pos = n_prompt - 1, token = argmax(logits, m->c.vocab_size);
    while (token != 1 && n < max_out) {                           /* BOS = stop */
        out[n++] = token;
        if (n >= max_out) break;              /* don't pay a forward for tokens we won't keep */
        pos++; if (pos >= m->c.seq_len - 1) break;
        logits = q4_model_forward(m, token, pos);
        token = argmax(logits, m->c.vocab_size);
    }
    return n;
}

int q4_model_decide(q4_model_t *m, const int *prompt, int n_prompt,
                    const int *cand, int n_cand, float temp, uint32_t *rng,
                    float *probs_out, int *out, int max_out) {
    if (!out || max_out < 0 || !token_ids_valid(m, prompt, n_prompt) ||
        !token_ids_valid(m, cand, n_cand)) return 0;
    int n = 0;
    float *logits = q4_model_prefill(m, prompt, n_prompt);
    if (!logits) for (int p = 0; p < n_prompt; p++) {
        logits = q4_model_forward(m, prompt[p], p);
        if (!logits) return 0;
    }
    /* softmax over the candidates only (the closed goal set) */
    float p[16]; if (n_cand > 16) n_cand = 16;
    float inv = temp > 0 ? 1.0f / temp : 1.0f, mx = -1e30f;
    for (int i = 0; i < n_cand; i++) { float v = logits[cand[i]] * inv; p[i] = v; if (v > mx) mx = v; }
    float sum = 0; for (int i = 0; i < n_cand; i++) { p[i] = fast_expf(p[i] - mx); sum += p[i]; }
    for (int i = 0; i < n_cand; i++) p[i] /= sum;
    if (probs_out) for (int i = 0; i < n_cand; i++) probs_out[i] = p[i];
    int pick = 0;
    if (temp > 0 && rng) {
        uint32_t x = *rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *rng = x;
        float r = (float)(x & 0xffffff) / 16777216.0f, acc = 0;
        for (int i = 0; i < n_cand; i++) { acc += p[i]; if (r < acc) { pick = i; break; } pick = i; }
    } else for (int i = 1; i < n_cand; i++) if (p[i] > p[pick]) pick = i;
    int pos = n_prompt - 1, token = cand[pick];
    while (token != 1 && n < max_out) {
        out[n++] = token;
        if (n >= max_out) break;              /* don't pay a forward for tokens we won't keep */
        pos++; if (pos >= m->c.seq_len - 1) break;
        logits = q4_model_forward(m, token, pos);
        token = argmax(logits, m->c.vocab_size);
    }
    return n;
}

/* reference path (no batching) for verification */
int q4_model_generate_seq(q4_model_t *m, const int *prompt, int n_prompt, int *out, int max_out) {
    if (!out || max_out < 0 || !token_ids_valid(m, prompt, n_prompt)) return 0;
    int pos = 0, token = prompt[0], n = 0;
    while (pos < m->c.seq_len - 1) {
        float *logits = q4_model_forward(m, token, pos);
        if (!logits) return 0;
        int next;
        if (pos < n_prompt - 1) next = prompt[pos + 1];
        else next = argmax(logits, m->c.vocab_size);
        pos++;
        if (pos >= n_prompt) { if (next == 1 || n >= max_out) break; out[n++] = next; }
        token = next;
    }
    return n;
}
