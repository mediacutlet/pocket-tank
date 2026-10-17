/* audio_port.h - the device side of the sound design (docs/AUDIO.md):
 * the mixer in common/audio.c fed to the ES8311 over I2S, the power amp
 * (the board's, board_pins.h), the codec's analog rail (ALDO1, where a PMIC
 * switches it) up only while something
 * plays. A player task on core 1 (the LLM core) renders 10 ms blocks; the
 * tank task only calls audio_port_play, which enqueues and returns.
 *
 * Power (docs/AUDIO.md section 3): idle = codec down, amp low, rail off,
 * I2S stopped - the state the battery pass measured. The first cue after
 * silence brings everything up (~30 ms); 2 s after the last voice ends it
 * all goes down again. audio_port_sleep() forces that before a drowse. */
#ifndef AUDIO_PORT_H
#define AUDIO_PORT_H
#include <stdbool.h>
#include "driver/i2c_master.h"

bool audio_port_init(i2c_master_bus_handle_t bus);   /* false = no codec / no bank: every call a no-op */
void audio_port_play(int cue, int pitch_q8);          /* SND_*, AUDIO_PITCH_ONE = native */
void audio_port_stop(int cue);                        /* fade a loop out */
/* the finger is down: bring the codec + amp up NOW so the cue the release
 * turns into (a card, a feed, a light) plays the instant it is asked for -
 * the cold-start settle hides under the finger's own dwell */
void audio_port_prewarm(void);
void audio_port_set_volume(int level);                /* 0 off, 1 quiet, 2 normal; saved to NVS */
int  audio_port_volume(void);
void audio_port_set_night(bool night);
void audio_port_sleep(void);                          /* silence + everything down; returns when it is */
void audio_port_deep_sleep_pins(void);                /* right before esp_deep_sleep_start: I2S + amp CTRL driven low and HELD (2026-09-16) */
void audio_port_tune(int codec_ms, int amp_ms, int idle_s);   /* -1 = keep; idle 0 = warm while awake; bench knobs */
bool audio_port_up(void);                             /* codec + amp currently powered (director / logs) */
const char *audio_port_state(void);                   /* one word for the log */

#endif
