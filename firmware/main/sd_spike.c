/* sd_spike.c - the tank switcher's step 2 spike (docs/tank-switcher.md): the
 * 1.8's TF slot, read and written at BOOT, before the tank exists (the card
 * is never mounted beside a running tank). `sd test` in the director saves,
 * sets the flag and restarts; this boot mounts the card, times a 2 MB write
 * and read, copies the running app to the card and from the card into the
 * other app slot through the real install path (esp_ota_*: the signature is
 * checked, the boot slot is NOT switched), times the model's 7.5 MB to the
 * card and back, deletes what it wrote, and the normal boot goes on.
 *
 * Pass 2 (2026-10-29, after 15.7 s for the app): what makes it faster, each
 * part on its own - the card's clock (20 MHz is the most: see below), reads with read()
 * in 32 KB pieces instead of stdio, the slot erased up front in 64 KB
 * blocks instead of 4 KB as it goes, and the card read on core 1 while
 * core 0 writes the flash (two buffers, handed over by queue).
 *
 * The 1.8's slot (schematic, SD-CARD): SPI - MOSI GPIO1, SCK GPIO2, MISO
 * GPIO3 - with the card's select on the TCA9554's EXIO7, not a GPIO. The
 * card is alone on its bus, so the select is held low for the whole visit;
 * the power-up clocks go out first with it high. VCC3V3, no card detect. */
#include "sd_spike.h"
#include "display_port.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "sdtest";
#define SD_MAGIC  0x53447431u                  /* "SDt1" */
#define SD_HOST   SPI3_HOST                    /* SPI2 drives the panel */
#define SD_MOSI   1
#define SD_SCK    2
#define SD_MISO   3
#define SD_MOUNT  "/sd"
#define SD_DIR    SD_MOUNT "/pocketank/test"
#define SD_CHUNK  32768                       /* (two of these in internal DMA memory: 64 KB each did not fit in one piece at boot) */
#define SD_BLOB   (2u * 1024 * 1024)
#define SD_MODEL  (7u * 1024 * 1024 + 512 * 1024)   /* the brain's 7.5 MB, for the copy time */
RTC_NOINIT_ATTR static uint32_t s_req, s_req_check;

void sd_spike_request(void) {
    s_req = SD_MAGIC; s_req_check = ~SD_MAGIC;
    ESP_LOGI(TAG, "restarting into the SD test");
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_restart();
}
bool sd_spike_pending(void) {
    bool yes = s_req == SD_MAGIC && s_req_check == ~SD_MAGIC;
    s_req = 0; s_req_check = 0;                /* one boot only */
    return yes;
}

static int64_t ms_since(int64_t t0) { return (esp_timer_get_time() - t0) / 1000; }
static float mbps(uint32_t bytes, int64_t ms) { return ms > 0 ? bytes / 1048576.0f / (ms / 1000.0f) : 0; }
static void fill(uint8_t *b, uint32_t off) { for (int i = 0; i < SD_CHUNK; i += 4) { uint32_t v = (off + i) * 2654435761u; memcpy(b + i, &v, 4); } }

/* the power-up clocks: 10 bytes of 0xFF at 400 kHz with the select high */
static void power_up_clocks(void) {
    spi_device_interface_config_t dev = { .clock_speed_hz = 400000, .mode = 0, .spics_io_num = -1, .queue_size = 1 };
    spi_device_handle_t h;
    if (spi_bus_add_device(SD_HOST, &dev, &h) != ESP_OK) return;
    uint8_t ff[10]; memset(ff, 0xff, sizeof ff);
    spi_transaction_t t = { .length = 80, .tx_buffer = ff };
    spi_device_polling_transmit(h, &t);
    spi_bus_remove_device(h);
}
static esp_err_t sd_mount(int khz, sdmmc_card_t **card) {
    board_sd_select(false);
    power_up_clocks();
    board_sd_select(true);
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST; host.max_freq_khz = khz;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SD_HOST; slot.gpio_cs = SDSPI_SLOT_NO_CS;
    esp_vfs_fat_mount_config_t mc = { .format_if_mount_failed = false, .max_files = 3, .allocation_unit_size = 16 * 1024 };
    return esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mc, card);
}
static void sd_unmount(sdmmc_card_t *card) { esp_vfs_fat_sdcard_unmount(SD_MOUNT, card); board_sd_select(false); }

/* 2 MB with write() / read() in 32 KB pieces, checked; MB/s each way */
static bool blob_test(uint8_t *buf, uint8_t *want, float *w, float *r) {
    *w = *r = 0;
    int fd = open(SD_DIR "/blob.bin", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { ESP_LOGE(TAG, "SDTEST: cannot open " SD_DIR "/blob.bin (errno %d)", errno); return false; }
    int64_t t0 = esp_timer_get_time();
    for (uint32_t off = 0; off < SD_BLOB; off += SD_CHUNK) { fill(buf, off); write(fd, buf, SD_CHUNK); }
    fsync(fd); close(fd); *w = mbps(SD_BLOB, ms_since(t0));
    if ((fd = open(SD_DIR "/blob.bin", O_RDONLY)) < 0) return false;
    bool ok = true; t0 = esp_timer_get_time();
    for (uint32_t off = 0; off < SD_BLOB; off += SD_CHUNK) {
        if (read(fd, buf, SD_CHUNK) != SD_CHUNK) { ok = false; break; }
        fill(want, off); if (memcmp(buf, want, SD_CHUNK)) ok = false;   /* (the check's own time is in the read's: ~1 ms a piece) */
    }
    close(fd); *r = mbps(SD_BLOB, ms_since(t0));
    unlink(SD_DIR "/blob.bin");
    return ok;
}

/* the card read on core 1 while core 0 writes the flash */
typedef struct { int i, k; } piece_t;
typedef struct { int fd; uint8_t *b[2]; QueueHandle_t full, empty; } reader_t;
static void reader_task(void *p) {
    reader_t *r = p; int i;
    while (xQueueReceive(r->empty, &i, portMAX_DELAY) == pdTRUE) {
        piece_t pc = { i, (int)read(r->fd, r->b[i], SD_CHUNK) };
        if (pc.k <= 0) pc.i = -1;
        xQueueSend(r->full, &pc, portMAX_DELAY);
        if (pc.i < 0) break;
    }
    vTaskDelete(NULL);
}

/* card -> the other slot; overlap = the reader on core 1. Times: erase (esp_ota_begin
 * with the size: 64 KB blocks up front), copy, check. */
static bool install(const char *path, const esp_partition_t *other, uint32_t len, uint8_t *b0, uint8_t *b1, bool overlap,
                    int64_t *erase_ms, int64_t *copy_ms, int64_t *check_ms) {
    *erase_ms = *copy_ms = *check_ms = -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    esp_ota_handle_t h;
    int64_t t0 = esp_timer_get_time();
    if (esp_ota_begin(other, len, &h) != ESP_OK) { close(fd); return false; }
    *erase_ms = ms_since(t0);
    bool ok = true; t0 = esp_timer_get_time();
    if (!overlap) {
        int k; uint32_t done = 0;
        while ((k = read(fd, b0, SD_CHUNK)) > 0) {
            if (esp_ota_write(h, b0, k) != ESP_OK) { ok = false; break; }
            if (((done += k) & 0x7ffff) == 0) vTaskDelay(1);   /* every 512 KB: the idle task its turn */
        }
    } else {
        reader_t r = { fd, { b0, b1 }, xQueueCreate(2, sizeof(piece_t)), xQueueCreate(2, sizeof(int)) };
        for (int i = 0; i < 2; i++) xQueueSend(r.empty, &i, 0);
        xTaskCreatePinnedToCore(reader_task, "sdread", 4096, &r, 5, NULL, 1);
        piece_t pc;
        while (xQueueReceive(r.full, &pc, portMAX_DELAY) == pdTRUE && pc.i >= 0) {
            if (ok && esp_ota_write(h, r.b[pc.i], pc.k) != ESP_OK) ok = false;
            xQueueSend(r.empty, &pc.i, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(20));                       /* the reader has deleted itself */
        vQueueDelete(r.full); vQueueDelete(r.empty);
    }
    *copy_ms = ms_since(t0);
    close(fd);
    t0 = esp_timer_get_time();
    esp_err_t e = esp_ota_end(h);                            /* the image checked: its hash and our signature */
    *check_ms = ms_since(t0);
    return ok && e == ESP_OK;
}
static bool same_image(const esp_partition_t *a, const esp_partition_t *b) {
    uint8_t s1[32], s2[32];
    return esp_partition_get_sha256(a, s1) == ESP_OK && esp_partition_get_sha256(b, s2) == ESP_OK && !memcmp(s1, s2, 32);
}

void sd_spike_run(void) {
    if (!board_has_expander()) { ESP_LOGW(TAG, "SDTEST: the spike knows only the 1.8's slot"); return; }
    ESP_LOGI(TAG, "SDTEST: start | internal heap %u KB, largest piece %u KB", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024);
    uint8_t *b0 = heap_caps_malloc(SD_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL), *b1 = heap_caps_malloc(SD_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *want = heap_caps_malloc(SD_CHUNK, MALLOC_CAP_SPIRAM);   /* (PSRAM: never a static in internal RAM) */
    spi_bus_config_t bus = { .mosi_io_num = SD_MOSI, .miso_io_num = SD_MISO, .sclk_io_num = SD_SCK,
                             .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096 };
    if (!b0 || !b1 || !want || spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SDTEST: no buffers or no SPI bus"); free(b0); free(b1); free(want); return;
    }
    /* 1. the clock: each one mounted, 2 MB each way, checked; the fastest good one kept. 20 MHz only:
       above 25 the card is switched to high speed, and on the 1.8's GPIO-matrix pins that left it
       answering garbage until its power was cut (2026-10-29: 26 MHz CSD error, then nothing mounted) */
    static const int KHZ[] = { 20000 };
    int best = 0; float best_r = 0;
    sdmmc_card_t *card = NULL;
    for (int c = 0; c < (int)(sizeof KHZ / sizeof KHZ[0]); c++) {
        int64_t t0 = esp_timer_get_time();
        esp_err_t e = sd_mount(KHZ[c], &card);
        if (e != ESP_OK) { ESP_LOGW(TAG, "SDTEST: %d MHz: mount FAILED (%s)", KHZ[c] / 1000, esp_err_to_name(e)); board_sd_select(false); continue; }
        int64_t mount_ms = ms_since(t0);
        mkdir(SD_MOUNT "/pocketank", 0777); mkdir(SD_DIR, 0777);
        float w, r; bool ok = blob_test(b0, want, &w, &r);
        ESP_LOGI(TAG, "SDTEST: %d MHz (card runs %d kHz): mount %lld ms | 2 MB write %.2f MB/s, read %.2f MB/s %s",
                 KHZ[c] / 1000, card->real_freq_khz, mount_ms, w, r, ok ? "ok" : "MISMATCH");
        if (ok && r > best_r) { best_r = r; best = KHZ[c]; }
        sd_unmount(card); card = NULL;
    }
    if (!best || sd_mount(best, &card) != ESP_OK) { ESP_LOGE(TAG, "SDTEST: no clock worked"); spi_bus_free(SD_HOST); free(b0); free(b1); free(want); return; }
    ESP_LOGI(TAG, "SDTEST: keeping %d MHz", best / 1000);

    /* 2. the running app to the card, then into the other slot two ways */
    const esp_partition_t *run = esp_ota_get_running_partition(), *other = esp_ota_get_next_update_partition(NULL);
    esp_partition_pos_t pos = { .offset = run->address, .size = run->size };
    esp_image_metadata_t md;
    uint32_t len = esp_image_get_metadata(&pos, &md) == ESP_OK ? ((md.image_len + 4095) & ~4095u) + 4096 : 0;
    if (len > run->size) len = run->size;
    int fd = len && other ? open(SD_DIR "/app.bin", O_WRONLY | O_CREAT | O_TRUNC) : -1;
    if (fd >= 0) {
        int64_t t0 = esp_timer_get_time();
        for (uint32_t off = 0; off < len; off += SD_CHUNK) {
            uint32_t k = len - off < SD_CHUNK ? len - off : SD_CHUNK;
            esp_partition_read(run, off, b0, k); write(fd, b0, k);
        }
        fsync(fd); close(fd);
        ESP_LOGI(TAG, "SDTEST: app %u KB: %s -> card %lld ms", (unsigned)(len >> 10), run->label, ms_since(t0));
        for (int ov = 0; ov < 2; ov++) {
            int64_t er, cp, ck;
            bool ok = install(SD_DIR "/app.bin", other, len, b0, b1, ov, &er, &cp, &ck);
            ESP_LOGI(TAG, "SDTEST: app card -> %s, erase up front%s: erase %lld + copy %lld + check %lld = %lld ms: %s, %s",
                     other->label, ov ? " + card read on core 1" : ", one core", er, cp, ck, er + cp + ck, ok ? "ok" : "FAILED", same_image(run, other) ? "identical" : "NOT identical");
        }
    }

    /* 3. the brain's size: model partition -> card, then read back in 32 KB pieces (nothing written to flash) */
    const esp_partition_t *mp = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "model");
    if (mp && (fd = open(SD_DIR "/model.bin", O_WRONLY | O_CREAT | O_TRUNC)) >= 0) {
        int64_t t0 = esp_timer_get_time();
        for (uint32_t off = 0; off < SD_MODEL; off += SD_CHUNK) {
            esp_partition_read(mp, off, b0, SD_CHUNK); write(fd, b0, SD_CHUNK);
            if ((off & 0x7ffff) == 0) vTaskDelay(1);
        }
        fsync(fd); close(fd); int64_t m_out = ms_since(t0), m_in = -1;
        if ((fd = open(SD_DIR "/model.bin", O_RDONLY)) >= 0) {
            t0 = esp_timer_get_time();
            while (read(fd, b0, SD_CHUNK) == SD_CHUNK) { }
            close(fd); m_in = ms_since(t0);
        }
        ESP_LOGI(TAG, "SDTEST: model 7.5 MB: flash -> card %lld ms (%.2f MB/s), card read %lld ms (%.2f MB/s)",
                 m_out, mbps(SD_MODEL, m_out), m_in, mbps(SD_MODEL, m_in));
    }

    unlink(SD_DIR "/app.bin"); unlink(SD_DIR "/model.bin");
    rmdir(SD_DIR); rmdir(SD_MOUNT "/pocketank");
    sd_unmount(card);
    spi_bus_free(SD_HOST);
    free(b0); free(b1); free(want);
    ESP_LOGI(TAG, "SDTEST: done, the card unmounted and left as it was; the normal boot goes on");
}
