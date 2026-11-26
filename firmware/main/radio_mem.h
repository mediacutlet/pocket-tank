/* radio_mem.h - where the hosted stack's memory goes on the big screens (0.4.3, 11-23).
 *
 * WHY: on 11-23 the 10.1 on the radio build restarted at `radio`. The C6 (our relay on hosted 3.0.9) advertised
 * 15,872 B SDIO SW_AGGR buffers; hosted probes one at once from internal DMA RAM and ASSERTS if it can't have it
 * ("no silent degrade"). The 0.4.3 tank runs with 77 KB internal free (October's tank had 141 with the stack down),
 * and the stack took ~71 KB of internal RAM then (docs/board-p4-10.1.md). So:
 *  - hosted's DMA buffers of RADIO_MEM_PSRAM_MIN and up (the SW_AGGR probe, the TX aggregate, the RX staging pair,
 *    the 1,536 B frame buffers) come from PSRAM, on the PSRAM cache line (128 B on the P4). IDF's SDMMC driver takes
 *    PSRAM on the P4 (SOC_SDMMC_PSRAM_DMA_CAPABLE) when the buffer's address and size are on that line, and syncs
 *    the cache around each transfer. (Hosted's own CONFIG_EH_HOST_PORT_DMA_PREFER_SPIRAM would do much the same: IDF
 *    5.5 drops the DMA flag from a SPIRAM request and aligns it to the line, esp_mm/heap_align_hw.c. An earlier note
 *    here said it could not - wrong, found by the 11-23 audit. The wrap is kept for its 1 KB floor and its counts.)
 *    The wraps are in radio_mem.c (--wrap, main/CMakeLists.txt, the radio image only).
 *  - small ones (register reads, an ESP-NOW frame) stay internal: they come and go, and need no line of their own.
 *  - the stack is not started at all unless internal RAM has room (radio_mem_room): LINK then gets a plain answer
 *    (radio_hosted_why) instead of hosted's assert restarting the tank.
 * The rules are plain C so firmware/host_test/radio_mem_host.c runs them on the Mac (tools/test_radio_ram.py). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define RADIO_MEM_PSRAM_MIN   1024             /* bytes: a hosted DMA buffer this big or bigger goes to PSRAM */
/* internal RAM the stack must find free before it starts, and the largest piece it must find. MEASURED 11-24 on
 * the 10.1 (32ab23e8, hosted 3.1.0, 5 starts of 5 + a 5-minute soak with a Wi-Fi scan): the stack takes 15-16 KB
 * internal (76 -> 61 KB free at the C6's answer, 60 with Wi-Fi up; the largest piece stays 27 KB, DMA 20 KB); its
 * big buffers (84 KB) and worker stacks are in PSRAM. October's stack took ~71 KB with them internal. So 40 KB
 * free leaves the tank ~24 KB after the start, and no start into pieces under 8 KB. */
#define RADIO_MEM_INT_NEED    (40 * 1024)
#define RADIO_MEM_INT_BLOCK   (8 * 1024)

/* should a hosted DMA buffer of n bytes, asked at `align`, come from PSRAM? If so, the size and alignment to ask
 * for: both on the PSRAM cache line `line` (0 if unknown: 64, the P4's L1 line, as the least). */
static inline bool radio_mem_psram(size_t n, size_t align, size_t line, size_t *out_n, size_t *out_align) {
    if (n < RADIO_MEM_PSRAM_MIN) return false;
    if (align == 0 || (align & (align - 1)) != 0) return false;   /* the library's own allocator refuses it */
    size_t a = line ? line : 64;
    if (a & (a - 1)) return false;
    if (align > a) a = align;
    *out_align = a;
    *out_n = (n + a - 1) & ~(a - 1);
    return true;
}

/* is there room to start the stack? (internal RAM free, and its largest free piece) */
static inline bool radio_mem_room(size_t int_free, size_t int_largest) {
    return int_free >= RADIO_MEM_INT_NEED && int_largest >= RADIO_MEM_INT_BLOCK;
}

/* one line on the console: internal free / largest, internal DMA largest, PSRAM free, and what the wraps placed
   (declared everywhere: firmware/host_test/radio_up_host.c gives its own) */
void radio_mem_log(const char *when);
