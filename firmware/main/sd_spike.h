/* sd_spike.h - the tank switcher's step 2 spike: the 1.8's TF slot at boot
 * (docs/tank-switcher.md). `sd test` in the director. */
#ifndef SD_SPIKE_H
#define SD_SPIKE_H
#include <stdbool.h>
void sd_spike_request(void);     /* the flag, then a restart */
bool sd_spike_pending(void);     /* this boot was asked for (clears the flag) */
void sd_spike_run(void);         /* mount, time, copy, clean up, unmount */
#endif
