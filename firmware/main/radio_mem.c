/* radio_mem.c - the hosted stack's big DMA buffers in PSRAM (radio_mem.h has the why; the radio image only). */
#include <stdint.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"   /* esp_cache_get_alignment: the line IDF's SDMMC checks against (sdmmc_host_check_buffer_alignment) */
#include "radio_mem.h"

static const char *TAG = "radio";
static volatile uint32_t s_ps_n, s_ps_bytes, s_int_n;   /* what the wraps placed (counts since boot, for the log) */

/* -Wl,--wrap: hosted's calls land here; __real_ is its own allocator (internal DMA RAM). */
void *__real_eh_host_port_dma_alloc_aligned(size_t n, size_t alignment);
void *__real_eh_host_port_dma_alloc(size_t n);

static void *to_psram(size_t n, size_t align) {
    size_t line = 0, sz, a;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &line) != ESP_OK) line = 0;
    if (!radio_mem_psram(n, align, line, &sz, &a)) return NULL;
    void *p = heap_caps_aligned_alloc(a, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) { s_ps_n++; s_ps_bytes += sz; }
    return p;
}
void *__wrap_eh_host_port_dma_alloc_aligned(size_t n, size_t alignment) {
    void *p = to_psram(n, alignment);
    if (p) return p;
    s_int_n++;
    return __real_eh_host_port_dma_alloc_aligned(n, alignment);
}
void *__wrap_eh_host_port_dma_alloc(size_t n) {
    void *p = to_psram(n, 4);
    if (p) return p;
    s_int_n++;
    return __real_eh_host_port_dma_alloc(n);
}
/* (eh_host_port_dma_free is heap_caps_free: it frees either kind) */

void radio_mem_log(const char *when) {
    ESP_LOGI(TAG, "mem %s: internal %u KB free (largest %u KB, DMA largest %u KB) | psram %u KB free | hosted DMA buffers: %lu in psram (%lu KB, since boot), %lu internal",
             when, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned long)s_ps_n, (unsigned long)(s_ps_bytes / 1024), (unsigned long)s_int_n);
}
