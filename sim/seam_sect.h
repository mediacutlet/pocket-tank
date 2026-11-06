/* seam_sect.h (audit harness, --selftest-seam): every mutable static of the per-tank modules (progression, notice,
   setup, collection, shelf) goes into two sections of their own, so the harness can run TWO tanks each with its own
   progression by swapping the sections' bytes (as a reboot would zero them). Force-included for those files only. */
#if defined(__APPLE__) && defined(__clang__)   /* (Mach-O sections: selftest_seam.c finds them with getsectiondata; elsewhere it skips) */
#pragma clang section bss="__DATA,__ptseamb" data="__DATA,__ptseamd"
#endif
