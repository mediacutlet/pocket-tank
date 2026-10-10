/* audio_port_stub.c - no codec (QEMU / bring-up): the sound design compiles,
 * every cue is a log line at most. */
#include "audio_port.h"
#include "audio.h"
#include "esp_log.h"

static int s_volume = 2;
bool audio_port_init(i2c_master_bus_handle_t bus) { (void)bus; ESP_LOGI("audio", "stub port: %d cues known, nothing plays", SND_COUNT); return false; }
void audio_port_play(int cue, int pitch_q8) { (void)cue; (void)pitch_q8; }
void audio_port_stop(int cue) { (void)cue; }
void audio_port_jingle(bool on) { (void)on; }
void audio_port_prewarm(void) {}
void audio_port_set_volume(int level) { s_volume = level < 0 ? 0 : level > 2 ? 2 : level; }
int  audio_port_volume(void) { return s_volume; }
void audio_port_set_night(bool night) { (void)night; }
void audio_port_sleep(void) {}
void audio_port_deep_sleep_pins(void) {}
void audio_port_tune(int codec_ms, int amp_ms, int idle_s) { (void)codec_ms; (void)amp_ms; (void)idle_s; }
bool audio_port_up(void) { return false; }
const char *audio_port_state(void) { return "stub"; }
