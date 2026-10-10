/* audio.c - the sound mixer. See audio.h for the contract. */
#include "audio.h"
#include "theme.h"
#include <string.h>
#include <math.h>

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
    uint32_t off, len;       /* clip, in bank samples */
    uint32_t pos;            /* 16.16 into the clip */
    uint32_t step;           /* 16.16 per output sample */
    int32_t  gain_q8;        /* cue gain x volume x night x master */
    uint32_t played;         /* output samples so far (fade-in) */
    int32_t  fade_left;      /* > 0: fading out over this many samples, then free */
    int32_t  fade_total;
    bool     loop;
} voice_t;

static const int16_t *s_bank;
static uint32_t s_bank_n;
static voice_t  s_v[AUDIO_VOICES];
static int      s_volume = 2;
static bool     s_night;
static uint32_t s_last_ms[SND_COUNT];        /* cooldown clocks */
static uint32_t s_starts[AUDIO_MAX_PER_S];   /* ring of recent start times */
static int      s_starts_i;
static uint32_t s_rng = 0x2545F491u;

static double pow10_(double db) {            /* 10^(db/20) without libm's pow in the hot path */
    double x = db / 20.0, r = 1.0, b = 10.0;
    int neg = x < 0; if (neg) x = -x;
    int n = (int)x; x -= n;
    while (n--) r *= b;
    /* fractional part by a short series of exp(x ln10) */
    double t = x * 2.302585092994046, e = 1.0, term = 1.0;
    for (int i = 1; i < 12; i++) { term *= t / i; e += term; }
    r *= e;
    return neg ? 1.0 / r : r;
}

void audio_init(const int16_t *bank, uint32_t n_samples) {
    s_bank = bank; s_bank_n = n_samples;
    for (int i = 0; i < AUDIO_VOICES; i++) s_v[i].cue = -1;
    memset(s_last_ms, 0, sizeof s_last_ms);
    memset(s_starts, 0, sizeof s_starts);
}
void audio_set_volume(int level) { s_volume = level < 0 ? 0 : level > 2 ? 2 : level; }
int  audio_volume(void) { return s_volume; }
void audio_set_night(bool night) { s_night = night; }
static bool jingle_live(void);
static void jingle_render(int32_t *mix, int m);
bool audio_active(void) { if (jingle_live()) return true; for (int i = 0; i < AUDIO_VOICES; i++) if (s_v[i].cue >= 0) return true; return false; }
const char *audio_cue_name(int cue) { return cue >= 0 && cue < SND_COUNT ? SND_CUES[cue].name : "?"; }
int audio_cue_by_name(const char *s) {
    for (int i = 0; i < SND_COUNT; i++) if (!strcmp(SND_CUES[i].name, s)) return i;
    return -1;
}

static int gain_for(int cue) {
    if (s_volume == 0) return 0;
    const policy_t *p = &POLICY[cue];
    if (s_night && p->tier == 2) return 0;
    int db = AUDIO_MASTER_DB + (s_volume == 1 ? -12 : 0) + (s_night && p->tier == 1 ? -12 : 0);
    int g = (int)(SND_CUES[cue].gain_q8 * pow10_(db));
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
    } else {                                               /* the global cap */
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
    if (clip->off + clip->len > s_bank_n || clip->len < 2) return false;
    voice_t *v = &s_v[slot];
    v->cue = (int8_t)cue; v->off = clip->off; v->len = clip->len; v->pos = 0;
    v->step = (uint32_t)((pitch_q8 <= 0 ? AUDIO_PITCH_ONE : pitch_q8) << 8);   /* q8 -> 16.16 */
    v->gain_q8 = g; v->played = 0; v->fade_left = 0; v->fade_total = 0; v->loop = c->loop != 0;
    s_last_ms[cue] = now_ms;
    if (!c->loop) { s_starts[s_starts_i] = now_ms ? now_ms : 1; s_starts_i = (s_starts_i + 1) % AUDIO_MAX_PER_S; }
    return true;
}

void audio_stop(int cue) {
    for (int i = 0; i < AUDIO_VOICES; i++)
        if (s_v[i].cue == cue && s_v[i].fade_left == 0) { s_v[i].fade_left = STOP_SAMPLES; s_v[i].fade_total = STOP_SAMPLES; }
}
static int s_jstate;                           /* the jingle: 0 off, 1 playing, 2 fading out */
void audio_stop_all(void) { for (int i = 0; i < AUDIO_VOICES; i++) s_v[i].cue = -1; s_jstate = 0; }

int audio_render(int16_t *out, int n) {
    int32_t mix[64];
    int live = 0;
    for (int done = 0; done < n; done += 64) {
        int m = n - done < 64 ? n - done : 64;
        memset(mix, 0, sizeof(int32_t) * m);
        for (int vi = 0; vi < AUDIO_VOICES; vi++) {
            voice_t *v = &s_v[vi];
            if (v->cue < 0) continue;
            const int16_t *clip = s_bank + v->off;
            for (int k = 0; k < m; k++) {
                uint32_t ip = v->pos >> 16;
                if (ip >= v->len) {
                    if (v->loop) { v->pos -= (uint32_t)v->len << 16; ip = v->pos >> 16; }
                    else { v->cue = -1; break; }
                }
                uint32_t frac = v->pos & 0xFFFF;
                int32_t a = clip[ip], b = clip[ip + 1 < v->len ? ip + 1 : (v->loop ? 0 : ip)];
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
        if (s_jstate) jingle_render(mix, m);
        for (int k = 0; k < m; k++) {
            int32_t s = mix[k];
            out[done + k] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
    }
    for (int vi = 0; vi < AUDIO_VOICES; vi++) if (s_v[vi].cue >= 0) live++;
    if (jingle_live()) live++;
    return live;
}

/* ---- the about page's jingle (2026-10-10) ----
 * Twelve bars of 4/4 at 96 BPM: a 16th is exactly 2500 samples at 16 kHz, so the
 * 192-step loop is 480000 samples = 30.000 s and wraps on a sample. Everything is
 * pitched above the 12 mm speaker's ~600 Hz floor (octaves 5 and 6). Voices:
 *   - the arpeggio: a kalimba-like pluck (a sine with a little 2nd and 3rd
 *     harmonic, a fast decay) on every 8th, climbing and falling through the
 *     bar's chord
 *   - the melody: a hummed sine with slow vibrato, a phrase over four bars,
 *     answered, then closed so the wrap lands back on the first note
 *   - the pad: the chord's root and fifth, swelling in over the bar, quiet
 *   - bubbles: short rising chirps at a few fixed steps
 * A slow amplitude wobble and a one-pole low-pass put it under water. The
 * sequencer position wraps at the loop's end; notes still ringing carry over,
 * so the seam is silent. */
#define J_STEPS     192                            /* 12 bars x 16 */
#define J_VOICES    10
#define J_SINE_N    1024
enum { JV_PLUCK = 1, JV_HUM, JV_PAD, JV_CHIRP };
typedef struct { uint8_t kind; uint32_t ph, inc; float env, amp; int32_t hold; float tgt; uint32_t age; } jv_t;
static int16_t  s_jsine[J_SINE_N];
static jv_t     s_jv[J_VOICES];
static uint32_t s_jpos;                           /* sample position in the loop */
static int32_t  s_jfade, s_jfade_total;            /* > 0: a fade (in while state 1 and jfade_in, out while state 2) */
static bool     s_jfade_in;
static float    s_jlp;                             /* the low-pass's state */
static uint32_t s_jlfo;
/* a tune per theme (2026-10-10 night, Alvin: "different creature, movement and music for each of
   the theme"): the same four voices, each theme its own tempo (the 16th's samples - an integer,
   so the loop still wraps on a sample), chords, arpeggio figure, melody, chirps and timbre:
   Original the kalimba at 96 BPM (30 s); Quiet Lagoon a slow lullaby over Lydian chords at 72
   (40 s), a muffled marimba, a deep wobble; Tidepool Club a perky steel-pan tune at 120 (24 s),
   bright, chirps everywhere; Blackwater a sparse minor-key bell piece at 60 (48 s), the pad up,
   the low-pass down - deep water. */
typedef struct { uint8_t step; int8_t note; uint8_t len; } jnote_t;
typedef struct {
    uint16_t step;                 /* samples per 16th */
    const int8_t (*chord)[4];      /* 12 bars */
    const int8_t *arp;             /* the bar's eight 8ths: a chord tone, or -1 for a rest */
    const jnote_t *mel; int nmel;
    const uint8_t *chirp; int nchirp; float chirp_hz;
    float pluck_h2, pluck_h3, pluck_decay, pluck_amp;
    float pad_amp, hum_vib, wob, lp;
} jsong_t;
/* the chords, one a bar: semitones above C5 (midi 72) for the arpeggio's four tones */
static const int8_t J_CHORD[12][4] = {
    { 0, 4, 7, 11 }, { -3, 0, 4, 7 }, { -7, -3, 0, 4 }, { -5, -1, 2, 4 },
    { 0, 4, 7, 11 }, { -8, -5, -1, 2 }, { -7, -3, 0, 4 }, { -10, -7, -3, 0 },
    { -3, 0, 4, 7 }, { -7, -3, 0, 4 }, { 0, 4, 7, 11 }, { -5, 0, 2, 5 } };
static const int8_t J_ARP[8] = { 0, 1, 2, 3, 2, 1, 0, 1 };   /* chord tones on the bar's 8ths; tone 3 an octave above tone -1 */
/* the melody: step, semitones above C5, length in 16ths */
static const jnote_t J_MELODY[] = {
    {   0, 16, 8 }, {   8, 19, 4 }, {  12, 14, 4 },                     /* E6  G6  D6 */
    {  16, 12, 8 }, {  24, 16, 6 },                                     /* C6  E6 */
    {  32,  9, 10 }, {  44, 12, 4 },                                    /* A5  C6 */
    {  48, 11, 14 },                                                    /* B5 */
    {  64, 19, 6 }, {  70, 16, 4 }, {  76, 14, 4 },                     /* G6  E6  D6 */
    {  80, 11, 8 }, {  88, 14, 6 },                                     /* B5  D6 */
    {  96, 12, 10 }, { 108,  9, 4 },                                    /* C6  A5 */
    { 112, 12, 12 },                                                    /* C6 */
    { 128, 16, 8 }, { 136, 19, 4 }, { 140, 21, 4 },                     /* E6  G6  A6 */
    { 144, 16, 8 }, { 152, 12, 6 },                                     /* E6  C6 */
    { 160,  9, 8 }, { 168, 12, 4 }, { 172, 11, 4 },                     /* A5  C6  B5 */
    { 176, 14, 10 }, { 188, 11, 3 } };                                  /* D6 ... B5, into the E6 at the wrap */
static const uint8_t J_CHIRP_STEPS[] = { 5, 23, 38, 55, 71, 87, 101, 118, 133, 150, 166, 183 };
/* Quiet Lagoon: F Lydian, long pentatonic phrases */
static const int8_t JL_CHORD[12][4] = {
    { 5, 9, 12, 16 }, { 0, 4, 7, 11 }, { 2, 5, 9, 12 }, { -5, -1, 2, 7 },
    { 5, 9, 12, 16 }, { 9, 12, 16, 19 }, { 2, 5, 9, 12 }, { -5, -1, 2, 7 },
    { 5, 9, 12, 16 }, { 0, 4, 7, 11 }, { -3, 0, 4, 7 }, { -5, 0, 2, 7 } };
static const int8_t JL_ARP[8] = { 0, 2, 1, 3, 2, 0, 1, 2 };
static const jnote_t JL_MELODY[] = {
    { 0, 12, 12 }, { 12, 9, 4 }, { 16, 14, 16 }, { 32, 12, 8 }, { 40, 9, 8 }, { 48, 7, 16 },
    { 64, 12, 12 }, { 76, 14, 4 }, { 80, 16, 12 }, { 92, 14, 4 }, { 96, 12, 16 }, { 112, 9, 16 },
    { 128, 14, 12 }, { 140, 16, 4 }, { 144, 19, 12 }, { 156, 17, 4 }, { 160, 14, 16 }, { 176, 12, 12 }, { 188, 9, 4 } };
static const uint8_t JL_CHIRP[] = { 10, 42, 74, 106, 138, 170 };
/* Tidepool Club: C major, a bouncing figure, a perky tune */
static const int8_t JC_CHORD[12][4] = {
    { 0, 4, 7, 12 }, { 5, 9, 12, 17 }, { -5, -1, 2, 7 }, { 0, 4, 7, 12 },
    { 9, 12, 16, 21 }, { 5, 9, 12, 17 }, { -5, -1, 2, 7 }, { 0, 4, 7, 12 },
    { 0, 4, 7, 12 }, { 5, 9, 12, 17 }, { -5, -1, 2, 7 }, { -5, 0, 2, 7 } };
static const int8_t JC_ARP[8] = { 0, 2, 1, 3, 0, 2, 1, 3 };
static const jnote_t JC_MELODY[] = {
    { 0, 12, 2 }, { 2, 12, 2 }, { 4, 16, 4 }, { 8, 19, 4 }, { 12, 16, 4 }, { 16, 17, 4 }, { 20, 16, 2 }, { 22, 14, 2 }, { 24, 12, 8 },
    { 32, 14, 4 }, { 36, 14, 2 }, { 38, 16, 2 }, { 40, 19, 8 }, { 48, 21, 4 }, { 52, 19, 4 }, { 56, 16, 8 },
    { 64, 12, 2 }, { 66, 12, 2 }, { 68, 16, 4 }, { 72, 19, 4 }, { 76, 24, 4 }, { 80, 21, 4 }, { 84, 19, 2 }, { 86, 17, 2 }, { 88, 16, 8 },
    { 96, 14, 4 }, { 100, 16, 4 }, { 104, 17, 4 }, { 108, 16, 2 }, { 110, 14, 2 }, { 112, 12, 12 },
    { 128, 19, 4 }, { 132, 19, 2 }, { 134, 21, 2 }, { 136, 24, 8 }, { 144, 21, 4 }, { 148, 19, 4 }, { 152, 17, 8 },
    { 160, 16, 4 }, { 164, 14, 4 }, { 168, 12, 4 }, { 172, 14, 4 }, { 176, 16, 8 }, { 184, 14, 4 }, { 188, 11, 4 } };
static const uint8_t JC_CHIRP[] = { 3, 11, 19, 27, 35, 43, 51, 59, 67, 75, 83, 91, 99, 107, 115, 123, 131, 139, 147, 155, 163, 171, 179, 187 };
/* Blackwater: D minor, a bell every other 8th, long low notes */
static const int8_t JB_CHORD[12][4] = {
    { 2, 5, 9, 14 }, { -2, 2, 5, 10 }, { -7, -3, 0, 5 }, { -5, -1, 2, 7 },
    { 2, 5, 9, 14 }, { -3, 0, 4, 9 }, { -2, 2, 5, 10 }, { -5, -1, 2, 7 },
    { 2, 5, 9, 14 }, { -7, -3, 0, 5 }, { -3, 0, 4, 9 }, { -5, 0, 2, 9 } };
static const int8_t JB_ARP[8] = { 0, -1, 2, -1, 1, -1, 3, -1 };
static const jnote_t JB_MELODY[] = {
    { 0, 14, 16 }, { 16, 12, 8 }, { 24, 10, 8 }, { 32, 9, 16 }, { 48, 5, 16 },
    { 64, 14, 12 }, { 76, 17, 4 }, { 80, 16, 16 }, { 96, 12, 16 }, { 112, 9, 16 },
    { 128, 17, 12 }, { 140, 14, 4 }, { 144, 12, 16 }, { 160, 10, 12 }, { 172, 9, 4 }, { 176, 5, 12 }, { 188, 9, 4 } };
static const uint8_t JB_CHIRP[] = { 9, 41, 73, 105, 137, 169 };
#define J_N(a) ((int)(sizeof (a) / sizeof (a)[0]))
static const jsong_t J_SONGS[THEME_COUNT] = {
    { 2500, J_CHORD,  J_ARP,  J_MELODY,  J_N(J_MELODY),  J_CHIRP_STEPS, J_N(J_CHIRP_STEPS), 900, 0.35f, 0.10f, 0.99975f, 0.22f, 0.085f, 0.004f, 0.12f, 0.55f },
    { 3333, JL_CHORD, JL_ARP, JL_MELODY, J_N(JL_MELODY), JL_CHIRP,      J_N(JL_CHIRP),      800, 0.20f, 0.04f, 0.99988f, 0.18f, 0.110f, 0.006f, 0.18f, 0.35f },
    { 2000, JC_CHORD, JC_ARP, JC_MELODY, J_N(JC_MELODY), JC_CHIRP,      J_N(JC_CHIRP),     1100, 0.55f, 0.06f, 0.99960f, 0.24f, 0.060f, 0.003f, 0.06f, 0.75f },
    { 4000, JB_CHORD, JB_ARP, JB_MELODY, J_N(JB_MELODY), JB_CHIRP,      J_N(JB_CHIRP),      700, 0.05f, 0.40f, 0.99992f, 0.20f, 0.140f, 0.005f, 0.20f, 0.30f } };
static const jsong_t *s_song = &J_SONGS[0];
#define J_STEP      ((uint32_t)s_song->step)
static float j_hz(int semis) { return 523.2511f * powf(2.0f, semis / 12.0f); }    /* from C5 */
static uint32_t j_inc(float hz) { return (uint32_t)(hz * (4294967296.0f / SND_RATE)); }
static jv_t *j_slot(void) {
    jv_t *best = &s_jv[0];
    for (int i = 0; i < J_VOICES; i++) { if (!s_jv[i].kind) return &s_jv[i]; if (s_jv[i].env < best->env) best = &s_jv[i]; }
    return best;
}
static void j_note(int kind, float hz, float amp, int hold_samples) {
    jv_t *v = j_slot();
    v->kind = (uint8_t)kind; v->ph = 0; v->inc = j_inc(hz); v->env = 0; v->amp = amp; v->hold = hold_samples; v->tgt = 1; v->age = 0;
}
static void j_step(int step) {
    int bar = step / 16, in = step % 16;
    const int8_t *ch = s_song->chord[bar];
    if (in % 2 == 0 && s_song->arp[in / 2] >= 0) {         /* the arpeggio, an 8th */
        int tone = s_song->arp[in / 2];
        int semis = ch[tone];
        if (tone == 3 && in / 2 == 3) semis = ch[0] + 12;   /* the top of the climb: the root an octave up */
        j_note(JV_PLUCK, j_hz(semis), in == 0 ? s_song->pluck_amp + 0.08f : s_song->pluck_amp, 0);
    }
    if (in == 0) {                                         /* the pad: root and fifth, for the bar */
        j_note(JV_PAD, j_hz(ch[0]), s_song->pad_amp, 15 * J_STEP);
        j_note(JV_PAD, j_hz(ch[2]), s_song->pad_amp * 0.76f, 15 * J_STEP);
    }
    for (int i = 0; i < s_song->nmel; i++)
        if (s_song->mel[i].step == step) j_note(JV_HUM, j_hz(s_song->mel[i].note), 0.26f, s_song->mel[i].len * J_STEP - 1500);
    for (int i = 0; i < s_song->nchirp; i++)
        if (s_song->chirp[i] == step) j_note(JV_CHIRP, s_song->chirp_hz + (i % 3) * 180, 0.10f, 0);
}
static inline float j_sin(uint32_t ph) { return s_jsine[ph >> 22] * (1.0f / 32767); }
void audio_jingle(bool on) {
    if (on) {
        if (s_jstate == 1) return;
        if (s_jstate == 0) {                               /* from silence: the top of the loop, a fresh voice table */
            for (int i = 0; i < J_SINE_N; i++) s_jsine[i] = (int16_t)(sinf(i * 6.2831853f / J_SINE_N) * 32767);
            memset(s_jv, 0, sizeof s_jv); s_jpos = 0; s_jlp = 0; s_jlfo = 0;
            s_song = &J_SONGS[theme_valid(theme_active())];      /* the theme's tune (a fade-out resumed keeps its own) */
        }
        s_jstate = 1; s_jfade_in = true; s_jfade = s_jfade_total = SND_RATE / 10;
    } else if (s_jstate == 1) { s_jstate = 2; s_jfade_in = false; s_jfade = s_jfade_total = SND_RATE * 2 / 5; }
}
bool audio_jingle_on(void) { return s_jstate == 1; }
static bool jingle_live(void) { return s_jstate != 0 && s_volume > 0; }
static void jingle_render(int32_t *mix, int m) {
    if (s_volume == 0) {                                   /* muted: keep time, make no sound */
        for (int k = 0; k < m; k++) { if (s_jpos % J_STEP == 0) j_step(s_jpos / J_STEP); if (++s_jpos >= (uint32_t)J_STEP * J_STEPS) s_jpos = 0; }
        if (s_jstate == 2) s_jstate = 0;
        return;
    }
    float master = (float)pow10_(AUDIO_MASTER_DB + (s_volume == 1 ? -12 : 0)) * 32767.0f;
    for (int k = 0; k < m; k++) {
        if (s_jpos % J_STEP == 0) j_step(s_jpos / J_STEP);
        float acc = 0;
        for (int i = 0; i < J_VOICES; i++) {
            jv_t *v = &s_jv[i];
            if (!v->kind) continue;
            float s;
            switch (v->kind) {
            case JV_PLUCK:                                 /* a kalimba: fast attack, a 0.3 s decay */
                s = j_sin(v->ph) + s_song->pluck_h2 * j_sin(v->ph * 2) + s_song->pluck_h3 * j_sin(v->ph * 3);
                if (v->age < 64) v->env = v->age / 64.0f; else v->env *= s_song->pluck_decay;
                if (v->age > 64 && v->env < 0.002f) v->kind = 0;
                break;
            case JV_HUM: {                                 /* a hummed sine, a slow vibrato, 20 ms in, 0.25 s out */
                uint32_t vib = (uint32_t)(sinf(v->age * (6.2831853f * 5.2f / SND_RATE)) * (float)v->inc * s_song->hum_vib);
                v->ph += vib;
                s = j_sin(v->ph) + 0.15f * j_sin(v->ph * 2);
                if (v->hold > 0) { v->hold--; v->env += (1 - v->env) * 0.003f; }
                else { v->env *= 0.99925f; if (v->env < 0.002f) v->kind = 0; }
                break; }
            case JV_PAD:                                   /* a slow swell, a slow fall */
                s = j_sin(v->ph);
                if (v->hold > 0) { v->hold--; v->env += (1 - v->env) * 0.00015f; }
                else { v->env *= 0.9997f; if (v->env < 0.002f) v->kind = 0; }
                break;
            default:                                       /* a bubble: a rising chirp over 70 ms */
                s = j_sin(v->ph);
                v->inc += v->inc / 1200;
                if (v->age < 40) v->env = v->age / 40.0f; else v->env *= 0.996f;
                if (v->age > 1100) v->kind = 0;
                break;
            }
            acc += s * v->env * v->amp;
            v->ph += v->inc; v->age++;
        }
        /* under water: a slow wobble and a soft low-pass */
        float wob = 1.0f + s_song->wob * j_sin(s_jlfo); s_jlfo += j_inc(7.0f * SND_RATE / (float)(J_STEP * J_STEPS));   /* seven wobbles a loop: periodic with it */
        s_jlp += (acc * wob - s_jlp) * s_song->lp;
        float g = master;
        if (s_jfade > 0) { float f = (float)s_jfade / s_jfade_total; g *= s_jfade_in ? 1 - f : f; if (--s_jfade == 0 && !s_jfade_in) { s_jstate = 0; } }
        mix[k] += (int32_t)(s_jlp * g);
        if (++s_jpos >= (uint32_t)J_STEP * J_STEPS) s_jpos = 0;
        if (!s_jstate) break;
    }
}
