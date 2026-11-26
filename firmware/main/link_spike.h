/* link_spike.h — tank link phase 0: the radio beside the live tank, measured
 * (link_spike.c, docs/tank-link-0.4.md). Director: link spike on|off|rate. */
#pragma once
#include <stdbool.h>
bool link_spike_on(int hz, int len, int preset);      /* hz beacons/s (0 = 2), len bytes each, preset: ignored (the lean radio only, since phase 0) */
void link_spike_rate(int hz, int len);
void link_spike_off(void);
bool link_spike_is_on(void);
