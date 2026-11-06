/* net_time.h - the time from the internet, for a board with no clock chip
 * (the round 1.75C, 2026-10-03; main.c's "clockless night"). One blocking
 * call at boot, before the tank exists - the radio never runs beside it:
 * the saved network, one NTP question, the radio off again. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* true: *unix_out is the time (seconds) the answer came at, *at_us the
 * esp_timer reading then (add what has passed since). false: no network
 * saved, no signal, or no answer - the radio is off either way. Three tries
 * of 10 s, 30 s in all, a router not seen swept for until the 30 s (0.3.9).
 * quick (0.4.1, clock_gate.h): two tries while the router is not seen, then
 * give up; one seen in them gets the whole of the above. */
bool net_time_sync(int64_t *unix_out, int64_t *at_us, bool quick);
/* the boot's time checks in a row that never saw the saved router (0 after
 * any connect, and after a network is saved): clock_gate.h's quick look */
int net_time_missed(void);
/* how the last net_time_sync went, in a few words: the try it got on at, or
 * why it failed (the router not seen / refused / silent, the driver's reason
 * number) - for the director's `clock` (0.3.9) */
const char *net_time_why(void);
