/* audio.h - the sound mixer (docs/AUDIO.md section 5), pure C, shared by
 * the device (I2S -> ES8311 -> NS4150B -> the 12 mm speaker) and the sim
 * (SDL). Plays cues from the bank tools/make_sounds.py builds
 * (assets/sounds/sounds.bin: 16 kHz mono s16 clips; common/sounds.h says
 * where each cue's clips are and how loud).
 *
 * Contract: audio_play is called from the tank/UI side and only writes a
 * voice slot; audio_render is called from the output side (a task, an SDL
 * callback) and mixes. The PLATFORM serializes the two (a mutex, an SDL
 * audio lock) - this module has no locks of its own.
 *
 * Rules the mixer enforces, so no caller has to:
 *  - per-cue cooldowns (eat once per 250 ms, snip 150 ms, ...) and a
 *    global cap of AUDIO_MAX_PER_S starts per second: excess is DROPPED,
 *    never queued late; the wheel tick is outside the cap (its 25 ms
 *    cooldown is its limit), so a fast drag ticks and spends no one's start
 *  - the tank light changes nothing: a dark tank sounds like a lit one
 *    (0.3.7; it used to mute tier 2 and dim tier 1)
 *  - volume: a percent, 0 off .. 100, the gain its square (50 % = -12 dB, the
 *    old QUIET; 100 % the old NORMAL); AUDIO_MASTER_DB under all
 *  - every voice fades in and out over 5 ms; a looping voice wraps at the
 *    clip's crossfaded seam and audio_stop fades it out over 50 ms
 *  - a cue with no clip (deferred) is a silent no-op */
#ifndef AUDIO_H
#define AUDIO_H
#include <stdbool.h>
#include <stdint.h>
#include "sounds.h"

#define AUDIO_VOICES     4
#define AUDIO_MAX_PER_S  6
#ifdef TANK_SQUARE
#define AUDIO_MASTER_DB  (-12)    /* the square's box drives its speaker harder: at the 1.8's -6 "a little loud, slightly blown
                                   * out"; its old quiet (-18) was right as normal (Strato by ear, 2026-10-12) */
#else
#define AUDIO_MASTER_DB  (-6)     /* headroom under the NS4150B's fixed gain; tune on the bench */
#endif
#define AUDIO_PITCH_ONE  256      /* pitch_q8 = 256 plays a clip at its own rate */

void audio_init(const int16_t *bank, uint32_t n_samples);
void audio_set_volume(int pct);          /* 0 (off) .. 100, clamped */
int  audio_volume(void);
int  audio_volume_from_level(int level); /* a 0.3.x level (0 off, 1 quiet, 2 normal) as the percent that sounds the same */
/* start a cue: returns true if a voice started. pitch_q8 scales the rate
 * (fry high, elder low); now_ms feeds the cooldowns (any monotonic ms). */
bool audio_play(int cue, int pitch_q8, uint32_t now_ms);
void audio_stop(int cue);                /* fade every voice of that cue out (loops) */
void audio_stop_all(void);
/* mix n mono s16 samples; returns the number of voices still live after
 * this block (0 = silence: the platform may power the codec down). */
int  audio_render(int16_t *out, int n);
bool audio_active(void);
const char *audio_cue_name(int cue);     /* "eat", or "?" */
int  audio_cue_by_name(const char *s);   /* -1 if unknown */

#endif
