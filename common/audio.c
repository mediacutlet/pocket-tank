/* audio.c - the sound mixer. See audio.h for the contract. */
#include "audio.h"
#include <string.h>

/* per-cue policy: tier (1 keeper feedback, 2 fish, 3 progression/system)
 * and cooldown in ms. Indexed by SND_*; order = sounds.h. */
typedef struct { uint8_t tier; uint16_t cooldown_ms; } policy_t;
static const policy_t POLICY[SND_COUNT] = {
    [SND_TAP]         = { 1, 80 },
    [SND_FEED]        = { 1, 200 },
    [SND_LIGHT_ON]    = { 1, 300 },
    [SND_LIGHT_OFF]   = { 1, 300 },
    [SND_WIPE]        = { 1, 400 },
    [SND_SNIP]        = { 1, 150 },
    [SND_CARD_OPEN]   = { 1, 150 },
    [SND_CARD_CLOSE]  = { 1, 150 },
    [SND_WHEEL_TICK]  = { 1, 25 },
    [SND_CONFIRM]     = { 1, 300 },
    [SND_BUBBLES_LOOP]= { 1, 0 },
    [SND_EAT]         = { 2, 250 },
    [SND_SPOOK]       = { 2, 2000 },
    [SND_INVESTIGATE] = { 2, 1500 },
    [SND_BUBBLES]     = { 2, 3000 },
    [SND_BEG]         = { 2, 10000 },
    [SND_WELCOME]     = { 3, 3000 },
    [SND_ARRIVAL]     = { 3, 3000 },
    [SND_MILESTONE]   = { 3, 1000 },
    [SND_STAGE_UP]    = { 3, 1000 },
    [SND_SLEEP]       = { 3, 1000 },
    [SND_WAKE]        = { 3, 1000 },
    [SND_LOW_BATTERY] = { 3, 5000 },
    [SND_ERROR]       = { 3, 1000 },
};

#define FADE_SAMPLES    (SND_RATE * 5 / 1000)     /* 5 ms */
#define STOP_SAMPLES    (SND_RATE * 50 / 1000)    /* 50 ms, a loop fading out */

typedef struct {
    int8_t   cue;            /* -1 = free */
    uint32_t len;            /* the clip, in samples */
    const uint8_t *data;     /* its bytes in the bank */
    uint8_t  codec;          /* SND_PCM | SND_ADPCM */
    uint8_t  idx;            /* ADPCM step index after sample n + 1 */
    int16_t  dp;             /* ADPCM predictor after sample n + 1 */
    uint32_t n;              /* the reader: samples n (a) and n + 1 (b) are decoded */
    int16_t  a, b;
    uint32_t pos;            /* 16.16 into the clip */
    uint32_t step;           /* 16.16 per output sample */
    int32_t  gain_q8;        /* cue gain x volume x master */
    uint32_t played;         /* output samples so far (fade-in) */
    int32_t  fade_left;      /* > 0: fading out over this many samples, then free */
    int32_t  fade_total;
    bool     loop;
} voice_t;

static const uint8_t *s_bank;
static uint32_t s_bank_n;            /* bytes */
static voice_t  s_v[AUDIO_VOICES];
static int      s_volume = 100;     /* percent: 0 off .. 100, the old NORMAL (0.4) */
static uint32_t s_last_ms[SND_COUNT];        /* cooldown clocks */
static uint32_t s_starts[AUDIO_MAX_PER_S];   /* ring of recent start times */
static int      s_starts_i;
static uint32_t s_rng = 0x2545F491u;

/* ---- the clip reader: one sample after another (0.4.1, IMA ADPCM) ----
 * A voice reads its clip forward only: pitch steps by up to a couple of
 * samples per output sample, and a loop wraps by starting over. So an ADPCM
 * clip decodes as it plays, a few bytes of state a voice, no buffer (the
 * internal RAM margin). Each SND_ADPCM_BLOCK-sample block opens with its
 * first sample exact and its step index, then the rest as nibbles, low
 * first (tools/make_sounds.py writes it; the standard IMA decoder). */
static const uint16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767 };
static const int8_t IMA_IDX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static int16_t le16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
/* sample k, the one after the last decoded (k = 0 or a block's first: exact) */
static int16_t rd_decode(voice_t *v, uint32_t k) {
    if (v->codec == SND_PCM) return le16(v->data + 2 * k);
    const uint8_t *blk = v->data + (k / SND_ADPCM_BLOCK) * SND_ADPCM_BLOCK_BYTES;
    uint32_t j = k % SND_ADPCM_BLOCK;
    if (j == 0) { v->dp = le16(blk); v->idx = blk[2] > 88 ? 88 : blk[2]; return v->dp; }
    j--;
    int code = (blk[4 + (j >> 1)] >> ((j & 1) * 4)) & 15;
    int32_t st = IMA_STEP[v->idx], d = st >> 3;
    if (code & 4) d += st;
    if (code & 2) d += st >> 1;
    if (code & 1) d += st >> 2;
    int32_t p = (code & 8) ? v->dp - d : v->dp + d;
    v->dp = (int16_t)(p < -32768 ? -32768 : p > 32767 ? 32767 : p);
    int ni = v->idx + IMA_IDX[code];
    v->idx = (uint8_t)(ni < 0 ? 0 : ni > 88 ? 88 : ni);
    return v->dp;
}
/* b, the sample after a: the next one, the loop's first again, or a held */
static int16_t rd_next(voice_t *v) {
    if (v->n + 1 < v->len) return rd_decode(v, v->n + 1);
    return v->loop ? le16(v->data) : v->a;               /* sample 0 is exact in either codec */
}
static void rd_start(voice_t *v) { v->n = 0; v->a = rd_decode(v, 0); v->b = rd_next(v); }
static void rd_to(voice_t *v, uint32_t ip) {             /* forward to a = sample ip */
    if (ip < v->n) rd_start(v);                          /* the loop wrapped */
    while (v->n < ip) { v->a = v->b; v->n++; v->b = rd_next(v); }
}
static uint32_t clip_bytes(const snd_clip_t *c) {
    if (c->codec == SND_PCM) return c->len * 2;
    uint32_t full = c->len / SND_ADPCM_BLOCK, rest = c->len % SND_ADPCM_BLOCK;
    return full * SND_ADPCM_BLOCK_BYTES + (rest ? 4 + rest / 2 : 0);
}
uint32_t audio_clip_check(int i) {                       /* the host test's: FNV-1a of the decoded clip */
    if (!s_bank || i < 0 || i >= SND_N_CLIPS) return 0;
    const snd_clip_t *c = &SND_CLIPS[i];
    if (c->off + clip_bytes(c) > s_bank_n) return 0;
    voice_t v = { 0 };
    v.data = s_bank + c->off; v.len = c->len; v.codec = c->codec;
    uint32_t h = 0x811C9DC5u;
    for (uint32_t k = 0; k < c->len; k++) {
        uint16_t x = (uint16_t)rd_decode(&v, k);
        h = (h ^ (x & 0xFF)) * 0x01000193u; h = (h ^ (x >> 8)) * 0x01000193u;
    }
    return h;
}

static double pow10_(double db) {            /* 10^(db/20) without libm's pow in the hot path */
    double x = db / 20.0, r = 1.0, b = 10.0;
    int neg = x < 0; if (neg) x = -x;
    int n = (int)x; x -= n;
    for (int i = 0; i < n && i < 32; i++) r *= b;   /* bounded (0.3.7): gcc -O2 on CI flagged the open loop once the
                                                    * night term left db two values; |db| stays far under 640 */
    /* fractional part by a short series of exp(x ln10) */
    double t = x * 2.302585092994046, e = 1.0, term = 1.0;
    for (int i = 1; i < 12; i++) { term *= t / i; e += term; }
    r *= e;
    return neg ? 1.0 / r : r;
}

void audio_init(const void *bank, uint32_t n_bytes) {
    s_bank = (const uint8_t *)bank; s_bank_n = n_bytes;
    for (int i = 0; i < AUDIO_VOICES; i++) s_v[i].cue = -1;
    memset(s_last_ms, 0, sizeof s_last_ms);
    memset(s_starts, 0, sizeof s_starts);
}
void audio_set_volume(int pct) { s_volume = pct < 0 ? 0 : pct > 100 ? 100 : pct; }
int  audio_volume_from_level(int level) { return level <= 0 ? 0 : level == 1 ? 50 : 100; }
int  audio_volume(void) { return s_volume; }
bool audio_active(void) { for (int i = 0; i < AUDIO_VOICES; i++) if (s_v[i].cue >= 0) return true; return false; }
const char *audio_cue_name(int cue) { return cue >= 0 && cue < SND_COUNT ? SND_CUES[cue].name : "?"; }
int audio_cue_by_name(const char *s) {
    for (int i = 0; i < SND_COUNT; i++) if (!strcmp(SND_CUES[i].name, s)) return i;
    return -1;
}

static int gain_for(int cue) {
    if (s_volume == 0) return 0;
    /* the light plays no part (0.3.7): until then a dark tank muted the
     * fish's cues and took 12 dB off the keeper's - a rule from the days the
     * light went out by itself on an idle desk. Since the double-tap the dark
     * is a keeper's choice while watching, and the tank sounds the same. */
    /* the slider (0.4) goes as the square of the percent: 50 % is -12 dB,
     * the old QUIET, and 100 % the old NORMAL - a saved level sounds the same */
    int g = (int)(SND_CUES[cue].gain_q8 * pow10_(AUDIO_MASTER_DB) * s_volume * s_volume / 10000);
    return g;
}

bool audio_play(int cue, int pitch_q8, uint32_t now_ms) {
    if (!s_bank || cue < 0 || cue >= SND_COUNT) return false;
    const snd_cue_t *c = &SND_CUES[cue];
    if (!c->n_var) return false;                           /* deferred: silent */
    const policy_t *p = &POLICY[cue];
    if (p->cooldown_ms && s_last_ms[cue] && now_ms - s_last_ms[cue] < p->cooldown_ms) return false;
    if (c->loop) {                                         /* one instance of a loop */
        for (int i = 0; i < AUDIO_VOICES; i++)
            if (s_v[i].cue == cue && s_v[i].fade_left == 0) return false;
    } else if (cue != SND_WHEEL_TICK) {                    /* the global cap; not the tick (0.4: a fast drag on the VOLUME
                                                              slider lost most of its ticks, and the lift's confirm with
                                                              them) - its own cooldown keeps it apart */
        uint32_t oldest = s_starts[s_starts_i];
        if (oldest && now_ms - oldest < 1000) return false;
    }
    int g = gain_for(cue);
    if (g <= 0) { s_last_ms[cue] = now_ms; return false; }  /* muted: still counts as played */
    int slot = -1;
    for (int i = 0; i < AUDIO_VOICES; i++) if (s_v[i].cue < 0) { slot = i; break; }
    if (slot < 0) {                                        /* steal the voice nearest its end */
        uint32_t best = 0;
        for (int i = 0; i < AUDIO_VOICES; i++) {
            if (s_v[i].loop) continue;
            uint32_t left = s_v[i].len - (s_v[i].pos >> 16);
            if (slot < 0 || left < best) { best = left; slot = i; }
        }
        if (slot < 0) return false;
    }
    s_rng = s_rng * 1664525u + 1013904223u;
    const snd_clip_t *clip = &SND_CLIPS[c->first + (c->n_var > 1 ? (s_rng >> 16) % c->n_var : 0)];
    if (clip->off + clip_bytes(clip) > s_bank_n || clip->len < 2) return false;
    voice_t *v = &s_v[slot];
    v->cue = (int8_t)cue; v->len = clip->len; v->pos = 0;
    v->data = s_bank + clip->off; v->codec = clip->codec; v->loop = c->loop != 0;
    rd_start(v);
    v->step = (uint32_t)((pitch_q8 <= 0 ? AUDIO_PITCH_ONE : pitch_q8) << 8);   /* q8 -> 16.16 */
    v->gain_q8 = g; v->played = 0; v->fade_left = 0; v->fade_total = 0; v->loop = c->loop != 0;
    s_last_ms[cue] = now_ms;
    if (!c->loop && cue != SND_WHEEL_TICK) { s_starts[s_starts_i] = now_ms ? now_ms : 1; s_starts_i = (s_starts_i + 1) % AUDIO_MAX_PER_S; }
    return true;
}

void audio_stop(int cue) {
    for (int i = 0; i < AUDIO_VOICES; i++)
        if (s_v[i].cue == cue && s_v[i].fade_left == 0) { s_v[i].fade_left = STOP_SAMPLES; s_v[i].fade_total = STOP_SAMPLES; }
}
void audio_stop_all(void) { for (int i = 0; i < AUDIO_VOICES; i++) s_v[i].cue = -1; }

int audio_render(int16_t *out, int n) {
    int32_t mix[64];
    int live = 0;
    for (int done = 0; done < n; done += 64) {
        int m = n - done < 64 ? n - done : 64;
        memset(mix, 0, sizeof(int32_t) * m);
        for (int vi = 0; vi < AUDIO_VOICES; vi++) {
            voice_t *v = &s_v[vi];
            if (v->cue < 0) continue;
            for (int k = 0; k < m; k++) {
                uint32_t ip = v->pos >> 16;
                if (ip >= v->len) {
                    if (v->loop) { v->pos -= (uint32_t)v->len << 16; ip = v->pos >> 16; }
                    else { v->cue = -1; break; }
                }
                uint32_t frac = v->pos & 0xFFFF;
                rd_to(v, ip);
                int32_t a = v->a, b = v->b;
                int32_t s = a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
                int32_t g = v->gain_q8;
                /* the clip's own fade-in for a non-loop is baked (5 ms); a
                   loop starts mid-texture, so it fades in here too */
                if (v->played < (uint32_t)FADE_SAMPLES) g = g * (int32_t)v->played / FADE_SAMPLES;
                if (v->fade_left > 0) {
                    g = g * v->fade_left / v->fade_total;
                    if (--v->fade_left == 0) { v->cue = -1; mix[k] += (s * g) >> 8; break; }
                }
                mix[k] += (s * g) >> 8;
                v->pos += v->step; v->played++;
            }
        }
        for (int k = 0; k < m; k++) {
            int32_t s = mix[k];
            out[done + k] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
    }
    for (int vi = 0; vi < AUDIO_VOICES; vi++) if (s_v[vi].cue >= 0) live++;
    return live;
}
