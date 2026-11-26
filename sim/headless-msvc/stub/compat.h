/* forced include for the MSVC headless build: GCC-isms the sim sources use */
#pragma once
#define __attribute__(x)
#define _Static_assert(c, m) struct compat_sa_dummy   /* save-layout asserts are about the device/mac ABI; snapshots never load a save */
#include <time.h>
#define CLOCK_MONOTONIC 1
static inline int clock_gettime(int id, struct timespec *ts) { (void)id; return timespec_get(ts, TIME_UTC) ? 0 : -1; }
