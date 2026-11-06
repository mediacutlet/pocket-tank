/* audio_host.c - host check of the sound mixer (common/audio.c) against the
 * real bank. Build + run from firmware/host_test:
 *   cc -O1 -std=c11 -I../../common -o audio_host audio_host.c ../../common/audio.c ../../common/sounds.c && ./audio_host
 * Checks: a deferred cue is silent; cooldowns and the per-second cap drop
 * extra starts; volume off is silence; 50 % is the old QUIET (-12 dB);
 * a loop keeps going past its length and audio_stop fades it to nothing;
 * a single cue at normal volume never clips; every clip decodes (IMA ADPCM
 * or PCM, 0.4.1) to exactly what tools/make_sounds.py's decoder made, and a
 * looping ADPCM clip wraps with no click at its seam. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

static int16_t *bank;
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int peak_of(int16_t *buf, int n) { int p = 0; for (int i = 0; i < n; i++) { int a = abs(buf[i]); if (a > p) p = a; } return p; }
static int render_all(int16_t *buf, int n) { return audio_render(buf, n); }

int main(void) {
    FILE *f = fopen("../../assets/sounds/sounds.bin", "rb");
    if (!f) { printf("no bank (tools/make_sounds.py build)\n"); return 1; }
    bank = malloc(SND_BANK_BYTES);
    if (fread(bank, 1, SND_BANK_BYTES, f) != SND_BANK_BYTES) { printf("bank size mismatch\n"); return 1; }
    fclose(f);
    static int16_t buf[16000 * 4];
    audio_init(bank, SND_BANK_BYTES);
    audio_set_volume(100);

    CHECK(!audio_play(SND_TAP, AUDIO_PITCH_ONE, 1000), "a deferred cue (tap) started");
    CHECK(audio_play(SND_EAT, AUDIO_PITCH_ONE, 1000), "eat did not start");
    CHECK(!audio_play(SND_EAT, AUDIO_PITCH_ONE, 1100), "eat restarted inside its 250 ms cooldown");
    CHECK(audio_play(SND_EAT, 320, 1300), "eat did not restart after the cooldown");
    int live = render_all(buf, 16000);
    CHECK(live == 0, "eat still live after a second (%d)", live);
    int pk = peak_of(buf, 16000);
    CHECK(pk > 1000 && pk < 32767, "eat peak %d (expected audible, unclipped)", pk);

    /* the per-second cap: 6 starts, the 7th dropped */
    int started = 0;
    static const int cues[7] = { SND_FEED, SND_LIGHT_ON, SND_LIGHT_OFF, SND_SNIP, SND_CARD_OPEN, SND_EAT, SND_CONFIRM };
    for (int i = 0; i < 7; i++) started += audio_play(cues[i], AUDIO_PITCH_ONE, 5000 + i * 10);
    CHECK(started == 6, "cap: %d of 7 started (want 6)", started);
    audio_stop_all();

    /* the wheel tick is outside the cap: a fast drag on the VOLUME slider
       (a step every 40 ms frame) ticks every step, and the lift's confirm
       still has its start */
    started = 0;
    for (int i = 0; i < 10; i++) started += audio_play(SND_WHEEL_TICK, AUDIO_PITCH_ONE, 8000 + i * 40);
    CHECK(started == 10, "ticks: %d of 10 started on a fast drag (want all)", started);
    CHECK(audio_play(SND_CONFIRM, AUDIO_PITCH_ONE, 8420), "the confirm after ten ticks was dropped");
    CHECK(!audio_play(SND_WHEEL_TICK, AUDIO_PITCH_ONE, 8370), "a tick inside its 25 ms cooldown started");
    audio_stop_all();

    /* volume off = silence */
    audio_set_volume(0);
    CHECK(!audio_play(SND_FEED, AUDIO_PITCH_ONE, 12000), "feed started with the volume off");

    /* the slider (0.4): 50 % is the old QUIET, -12 dB - a quarter of 100 %'s peak */
    audio_set_volume(100);
    CHECK(audio_play(SND_EAT, AUDIO_PITCH_ONE, 14000), "eat did not start at 100 %%");
    render_all(buf, 16000); int pk100 = peak_of(buf, 16000);
    audio_set_volume(50);
    CHECK(audio_play(SND_EAT, AUDIO_PITCH_ONE, 16000), "eat did not start at 50 %%");
    render_all(buf, 16000); int pk50 = peak_of(buf, 16000);
    CHECK(pk50 * 4 > pk100 * 0.9 && pk50 * 4 < pk100 * 1.1, "50 %% peak %d, 100 %% peak %d (want a quarter, -12 dB)", pk50, pk100);
    CHECK(audio_volume_from_level(1) == 50 && audio_volume_from_level(2) == 100 && audio_volume_from_level(0) == 0, "0.3.x levels misread");
    audio_set_volume(100);

    /* the loop: past its own length it is still live; stop fades it out */
    CHECK(audio_play(SND_BUBBLES_LOOP, AUDIO_PITCH_ONE, 20000), "loop did not start");
    CHECK(!audio_play(SND_BUBBLES_LOOP, AUDIO_PITCH_ONE, 20001), "a second loop instance started");
    live = render_all(buf, 16000 * 3);
    CHECK(live == 1, "loop not live after 3 s (%d)", live);
    int seam_pk = peak_of(buf, 16000 * 3);
    CHECK(seam_pk > 500, "loop is silent (%d)", seam_pk);
    audio_stop(SND_BUBBLES_LOOP);
    live = render_all(buf, 1600);                         /* 100 ms > the 50 ms fade */
    CHECK(live == 0, "loop still live 100 ms after stop (%d)", live);
    CHECK(peak_of(buf + 1200, 400) == 0, "loop not silent after its fade");

    /* the biggest cue alone never clips */
    for (int c = 0; c < SND_COUNT; c++) {
        if (!SND_CUES[c].n_var || SND_CUES[c].loop) continue;
        audio_stop_all(); audio_play(c, AUDIO_PITCH_ONE, 30000 + c * 4000);
        render_all(buf, 16000 * 4);
        int p = peak_of(buf, 16000 * 4);
        CHECK(p < 32767, "%s clips (%d)", SND_CUES[c].name, p);
        CHECK(p > 0, "%s is silent", SND_CUES[c].name);
    }
    /* 0.4.1: the decoder is the tool's, sample for sample */
    int adpcm = 0;
    for (int i = 0; i < SND_N_CLIPS; i++) {
        CHECK(audio_clip_check(i) == SND_CLIPS[i].check, "clip %d (%s) decodes to %08X, the tool made %08X", i,
              SND_CLIPS[i].codec == SND_ADPCM ? "ADPCM" : "PCM", (unsigned)audio_clip_check(i), (unsigned)SND_CLIPS[i].check);
        adpcm += SND_CLIPS[i].codec == SND_ADPCM;
    }
    CHECK(adpcm > 0, "no ADPCM clip in the bank");
    CHECK(SND_CLIPS[SND_CUES[SND_INVESTIGATE].first].codec == SND_PCM, "investigate is not PCM (Strato's ear, 2026-11-04)");

    /* the loop's seam through the reader: no jump bigger than the clip's own */
    audio_stop_all(); audio_set_volume(100);
    const snd_clip_t *lc = &SND_CLIPS[SND_CUES[SND_BUBBLES_LOOP].first];
    CHECK(audio_play(SND_BUBBLES_LOOP, AUDIO_PITCH_ONE, 90000), "loop did not start (seam)");
    int n_seam = (int)lc->len * 3;                        /* three times round, at its own rate */
    if (n_seam > 16000 * 4) n_seam = 16000 * 4;
    render_all(buf, n_seam);
    int max_jump = 0, seam_jump = 0;
    for (int i = 1600; i < n_seam; i++) {                 /* past the 5 ms fade-in */
        int j = abs(buf[i] - buf[i - 1]);
        if (j > max_jump) max_jump = j;
        if ((uint32_t)i % lc->len == 0 && j > seam_jump) seam_jump = j;
    }
    CHECK(seam_jump <= max_jump && max_jump > 0, "loop seam jump %d over the clip's own %d", seam_jump, max_jump);
    audio_stop_all();

    if (fails) printf("audio_host: %d FAILED\n", fails);
    else printf("audio_host: all checks passed (%u cues, bank %u KB)\n", (unsigned)SND_COUNT, (unsigned)(SND_BANK_BYTES / 1024));
    return fails != 0;
}
