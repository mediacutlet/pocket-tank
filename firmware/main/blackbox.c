/* blackbox.c - THE BLACK BOX (blackbox.h has what it keeps; blackbox_ring.h where and how). */
#include "blackbox.h"
#include "i2c_guard.h"
#include "version.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_debug_helpers.h"
#include "esp_cpu_utils.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_partition.h"
#include "esp_private/panic_internal.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/rwdt_ll.h"
#include "hal/wdt_hal.h"
#include "soc/rtc.h"
#if __XTENSA__
#include "xtensa_context.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "blackbox";

/* ---- the run's breadcrumb, in RTC memory: kept through every reset but a power loss. sum: a power-on's garbage is
   never taken for a breadcrumb ---- */
typedef struct { uint32_t magic, up_s, frames, sum; uint8_t phase, panel_restarts, late_restarts, pad; } crumb_t;
#define CRUMB_MAGIC 0x62626F78u          /* "bbox" */
RTC_NOINIT_ATTR static crumb_t s_crumb;
static uint32_t crumb_sum(const crumb_t *c) {
    return c->magic ^ c->up_s ^ (c->frames * 2654435761u) ^ ((uint32_t)c->phase | (uint32_t)c->panel_restarts << 8 | (uint32_t)c->late_restarts << 16) ^ 0x5EEDF00Du;
}
static inline void crumb_seal(void) { s_crumb.sum = crumb_sum(&s_crumb); }
static bool crumb_ok(void) { return s_crumb.magic == CRUMB_MAGIC && s_crumb.sum == crumb_sum(&s_crumb) && s_crumb.phase < BB_PH_COUNT; }

/* ---- a panic, waiting in RTC memory for the next boot to write it ---- */
typedef struct { uint32_t magic; uint16_t crc, pad; bb_panic_t p; } rpanic_t;
#define PANIC_MAGIC 0x50414E43u          /* "PANC" */
RTC_NOINIT_ATTR static rpanic_t s_rp;

/* ---- the flash ---- */
static const esp_partition_t *s_part;
static bool fl_read(void *c, uint32_t off, void *d, uint32_t n)        { (void)c; return esp_partition_read(s_part, off, d, n) == ESP_OK; }
static bool fl_write(void *c, uint32_t off, const void *s, uint32_t n) { (void)c; return esp_partition_write(s_part, off, s, n) == ESP_OK; }
static bool fl_erase(void *c, uint32_t off)                            { (void)c; return esp_partition_erase_range(s_part, off, BB_SECTOR) == ESP_OK; }
static const bb_flash_t s_fl = { fl_read, fl_write, fl_erase, NULL };
static bb_ring_t s_boots, s_beats;
static bool s_on;
static bb_boot_t s_boot;                  /* this boot's record, filled as the boot goes */
static bb_panic_t s_panic; static bool s_have_panic;
static uint32_t s_boot_seq;
static bool s_committed;

static void on_shutdown(void) { if (crumb_ok() && s_crumb.phase != BB_PH_PANEL_RESTART) { s_crumb.phase = BB_PH_RESTART; crumb_seal(); } }   /* every esp_restart: the next boot knows it was asked for */

void blackbox_boot_begin(void) {
    memset(&s_boot, 0, sizeof s_boot);
    s_boot.reset = (uint8_t)esp_reset_reason();
    s_boot.rom0 = (uint8_t)esp_rom_get_reset_reason(0);
    s_boot.rom1 = (uint8_t)esp_rom_get_reset_reason(1);
    s_boot.wake = (uint8_t)esp_sleep_get_wakeup_cause();
    const esp_app_desc_t *d = esp_app_get_description();
    memcpy(&s_boot.build, d->app_elf_sha256, 4);
    const char *g = strstr(d->version, "-g");     /* the git describe's hash (or the version itself: a plain hash), else the release */
    snprintf(s_boot.ver, sizeof s_boot.ver, "%.7s", g ? g + 2 : d->version[0] ? d->version : PT_RELEASE);   /* (7 + the NUL: snprintf's own) */
    uint8_t panel_restarts = 0, late_restarts = 0;
    if (crumb_ok()) {
        s_boot.flags |= BB_BF_CRUMB;
        s_boot.crumb_phase = s_crumb.phase; s_boot.crumb_up_s = s_crumb.up_s; s_boot.crumb_frames = s_crumb.frames;
        panel_restarts = s_crumb.panel_restarts; late_restarts = s_crumb.late_restarts;
    }
    if (s_rp.magic == PANIC_MAGIC && s_rp.crc == bb_crc16(&s_rp.p, sizeof s_rp.p)) {
        s_panic = s_rp.p; s_have_panic = true; s_boot.flags |= BB_BF_PANIC;
    }
    s_rp.magic = 0;
    s_boot.panel_restarts = panel_restarts;
    memset(&s_crumb, 0, sizeof s_crumb);
    s_crumb.magic = CRUMB_MAGIC; s_crumb.phase = BB_PH_BOOT; s_crumb.panel_restarts = panel_restarts; s_crumb.late_restarts = late_restarts;
    crumb_seal();
    esp_register_shutdown_handler(on_shutdown);
    esp_err_t e = esp_partition_register_external(esp_flash_default_chip, BB_FLASH_BASE, BB_FLASH_SIZE, "bbox", ESP_PARTITION_TYPE_DATA,
                                                  (esp_partition_subtype_t)0x99, &s_part);
    if (e != ESP_OK) { ESP_LOGW(TAG, "no black box: the flash gap 0x%X would not register (%s)", (unsigned)BB_FLASH_BASE, esp_err_to_name(e)); return; }
    bb_ring_open(&s_boots, &s_fl, 0, BB_BOOT_SECTORS, BB_BOOT_SLOT);
    bb_ring_open(&s_beats, &s_fl, BB_BOOT_SECTORS * BB_SECTOR, BB_BEAT_SECTORS, BB_BEAT_SLOT);
    s_on = true;
}
void blackbox_boot_pmic(const uint8_t regs[BB_PMIC_N], uint16_t ok) {
    memcpy(s_boot.pmic, regs, BB_PMIC_N); s_boot.pmic_ok = ok;
    if (ok) s_boot.flags |= BB_BF_PMIC;
}
void blackbox_boot_board(int tries) {
    s_boot.tries = (uint8_t)(tries < 0 ? 0 : tries > 255 ? 255 : tries);
    if (tries != 1) s_boot.flags |= BB_BF_NO_BOARD;
}
bool blackbox_panel_restart_take(void) {
    if (s_crumb.panel_restarts >= BB_PANEL_RESTARTS) return false;
    s_crumb.panel_restarts++; crumb_seal();
    return true;
}
bool blackbox_late_restart_take(void) {
    if (s_crumb.late_restarts >= 1) return false;     /* once a power-on: the restarts can never loop */
    s_crumb.late_restarts++; crumb_seal();       /* (the boot's own restarts stay spent: at most 3 a power-on in all) */
    return true;
}
void blackbox_boot_commit(bool restarting) {
    if (s_committed) return;
    s_committed = true;
    s_boot.crumb_i2c = i2c_guard_prev_inflight(); s_boot.crumb_i2c_fails = i2c_guard_prev_fails();
    if (i2c_guard_boot_stuck()) s_boot.flags |= BB_BF_BUS_STUCK;
    if (i2c_guard_boot_freed()) s_boot.flags |= BB_BF_BUS_FREED;
    if (restarting) s_boot.flags |= BB_BF_NO_PANEL;
    s_boot.panel_restarts = s_crumb.panel_restarts;
    if (!restarting && s_boot.tries > 0) { s_crumb.panel_restarts = 0; crumb_seal(); }   /* the board answered: the count starts over */
    if (!s_on) return;
    if (!bb_ring_append(&s_boots, &s_boot, BB_T_BOOT)) { ESP_LOGW(TAG, "the boot record could not be written"); return; }
    s_boot_seq = s_boots.seq;
    if (s_have_panic && !bb_ring_append(&s_boots, &s_panic, BB_T_PANIC)) ESP_LOGW(TAG, "the panic record could not be written");
    ESP_LOGI(TAG, "boot %lu recorded: reset reason %d (ROM %02x/%02x), the run before %s%s | %lu boots and %lu heartbeats kept (director `blackbox`)",
             (unsigned long)s_boot_seq, s_boot.reset, s_boot.rom0, s_boot.rom1,
             s_boot.flags & BB_BF_CRUMB ? bb_phase_name(s_boot.crumb_phase) : "left no breadcrumb (a power-on)", s_have_panic ? ", WITH A PANIC RECORD" : "",
             (unsigned long)s_boots.count, (unsigned long)s_beats.count);
}

void blackbox_crumb_phase(int ph) { if (s_crumb.phase != ph) { s_crumb.phase = (uint8_t)ph; crumb_seal(); } }
void blackbox_frame(void) { s_crumb.frames++; s_crumb.phase = BB_PH_FRAME; crumb_seal(); }

/* ---- the RTC watchdog (ESP-IDF's own HAL, as its sleep code and esp_restart use it) ---- */
static wdt_hal_context_t s_rwdt = RWDT_HAL_CONTEXT_DEFAULT();
static bool s_rwdt_on;
void blackbox_feed(int64_t now_us) {
    if (s_rwdt_on) { wdt_hal_write_protect_disable(&s_rwdt); wdt_hal_feed(&s_rwdt); wdt_hal_write_protect_enable(&s_rwdt); }
    const uint32_t up = (uint32_t)(now_us / 1000000);
    if (s_crumb.up_s != up) { s_crumb.up_s = up; crumb_seal(); }
}
void blackbox_rwdt_arm(void) {
    const uint32_t hz = rtc_clk_slow_freq_get_hz();
    wdt_hal_init(&s_rwdt, WDT_RWDT, 0, false);
    wdt_hal_write_protect_disable(&s_rwdt);
    wdt_hal_config_stage(&s_rwdt, WDT_STAGE0, BB_RWDT_S * hz, WDT_STAGE_ACTION_RESET_SYSTEM);   /* RTC memory kept: the breadcrumb says where */
    wdt_hal_config_stage(&s_rwdt, WDT_STAGE1, 10 * hz, WDT_STAGE_ACTION_RESET_RTC);            /* ... and if that did not take, all of it */
    wdt_hal_set_flashboot_en(&s_rwdt, false);
    rwdt_ll_set_pause_in_sleep_en(s_rwdt.rwdt_dev, false);   /* counts through light sleep: a sleep that never wakes is caught (wdt_hal_init pauses it) */
    wdt_hal_enable(&s_rwdt);
    wdt_hal_write_protect_enable(&s_rwdt);
    s_rwdt_on = true;
    ESP_LOGI(TAG, "RTC watchdog: on, %d s unfed (light sleep included) restarts the chip", BB_RWDT_S);
}
void blackbox_rwdt_disarm(void) {
    if (!s_rwdt_on) return;
    wdt_hal_write_protect_disable(&s_rwdt); wdt_hal_disable(&s_rwdt); wdt_hal_write_protect_enable(&s_rwdt);
    s_rwdt_on = false;
}

/* ---- the panic: wrapped (-Wl,--wrap=esp_panic_handler). panicHandler has put the flash cache back before this runs
   (esp_system/port/panic_handler.c), and nothing here waits or prints: the record, then ESP-IDF's own handler ---- */
void __real_esp_panic_handler(panic_info_t *info);
void __wrap_esp_panic_handler(panic_info_t *info) {
    bb_panic_t *p = &s_rp.p;
    memset(p, 0, sizeof *p);
    if (info) {
        p->core = (uint8_t)info->core; p->exception = (uint8_t)info->exception; p->pc = (uint32_t)info->addr;
#if __XTENSA__   /* (the frame and the walk below are the Xtensa core's: the pocket boards. A RISC-V chip, for a later P4 port, would keep the PC and the task) */
        const XtExcFrame *f = info->frame;
        const bool fr_ok = f && esp_ptr_byte_accessible(f);
        if (fr_ok) { p->pc = f->pc; p->excvaddr = f->excvaddr; p->exccause = (uint8_t)f->exccause; }
#endif
        TaskHandle_t t = info->core >= 0 && info->core < portNUM_PROCESSORS ? xTaskGetCurrentTaskHandleForCore(info->core) : NULL;
        const char *nm = t ? pcTaskGetName(t) : NULL;
        for (int i = 0; nm && i < (int)sizeof p->task - 1 && esp_ptr_byte_accessible(nm + i) && nm[i]; i++) p->task[i] = nm[i];
        s_rp.crc = bb_crc16(p, sizeof *p); s_rp.magic = PANIC_MAGIC;   /* sealed before the stack walk: a walk that faults on a broken
                                                                         stack still leaves the PC and the task */
#if __XTENSA__
        if (fr_ok) {
            esp_backtrace_frame_t fr = { .pc = f->pc, .sp = f->a1, .next_pc = f->a0, .exc_frame = f };
            p->bt[p->nbt++] = esp_cpu_process_stack_pc(fr.pc);
            s_rp.crc = bb_crc16(p, sizeof *p);
            while (p->nbt < 8 && fr.next_pc && esp_stack_ptr_is_sane(fr.sp) && esp_backtrace_get_next_frame(&fr)) {
                p->bt[p->nbt++] = esp_cpu_process_stack_pc(fr.pc);
                s_rp.crc = bb_crc16(p, sizeof *p);
            }
        }
#endif
    }
    s_rp.crc = bb_crc16(p, sizeof *p); s_rp.magic = PANIC_MAGIC;
    __real_esp_panic_handler(info);
}

/* ---- heartbeats ---- */
static int64_t s_last_beat = INT64_MIN / 2, s_last_burst = INT64_MIN / 2, s_last_edge = INT64_MIN / 2;
static bool s_prev_night, s_prev_link, s_have_prev;
static uint32_t s_prev_heals, s_prev_fails, s_prev_recov;
static void beat(int ev, const bb_state_t *s, int64_t now) {
    if (!s_on || !s_committed) return;
    bb_beat_t b; memset(&b, 0, sizeof b);
    b.up_s = (uint32_t)(now / 1000000);
    const time_t t = time(NULL); b.wall = t > 1700000000 ? (uint32_t)t : 0;
    b.event = (uint8_t)ev;
    b.state = (s->night ? BB_S_NIGHT : 0) | (s->link ? BB_S_LINK : 0) | (s->charging ? BB_S_CHARGING : 0) | (s->cable ? BB_S_CABLE : 0) |
              (s->grace ? BB_S_GRACE : 0) | (s->no_panel ? BB_S_NO_PANEL : 0) | (s->locked ? BB_S_LOCKED : 0);
    b.pct = s->pct < 0 || s->pct > 100 ? 255 : (uint8_t)s->pct;
    const size_t kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024; b.heap_kb = kb > 255 ? 255 : (uint8_t)kb;
    b.mv = s->mv < 0 ? 0 : s->mv > 65535 ? 65535 : (uint16_t)s->mv;
    const uint32_t f = i2c_guard_fails(), r = i2c_guard_recoveries();
    b.i2c_fails = f > 65535 ? 65535 : (uint16_t)f; b.i2c_recov = r > 255 ? 255 : (uint8_t)r; b.i2c_last = i2c_guard_last_addr();
    b.fps = s->fps < 0 ? 0 : s->fps > 254.5f ? 255 : (uint8_t)(s->fps + 0.5f);
    b.heals = s->heals > 255 ? 255 : (uint8_t)s->heals;
    b.boot_seq = (uint16_t)s_boot_seq;
    if (!bb_ring_append(&s_beats, &b, BB_T_BEAT)) ESP_LOGW(TAG, "a heartbeat could not be written");
    s_last_beat = now;
}
void blackbox_event(int ev, const bb_state_t *s) { beat(ev, s, esp_timer_get_time()); }
void blackbox_tick(int64_t now, const bb_state_t *s) {
    if (!s_on || !s_committed) return;
    const uint32_t f = i2c_guard_fails(), r = i2c_guard_recoveries();
    int ev = -1;
    const bool edge_ok = now - s_last_edge >= 60000000LL;   /* the tick's edges: one beat a minute at most (a storm of recoveries must not
                                                               wear the ring or push the hours before a dark episode out of it); the
                                                               counters and the state bits carry what happened between */
    if (!s_have_prev) ev = BB_EV_BOOTED;
    else if (!edge_ok) { if (now - s_last_beat >= (int64_t)BB_BEAT_S * 1000000) ev = BB_EV_TICK; goto beat_now; }
    else if (s->night != s_prev_night) ev = s->night ? BB_EV_LIGHT_OFF : BB_EV_LIGHT_ON;
    else if (s->link != s_prev_link) ev = s->link ? BB_EV_LINK_UP : BB_EV_LINK_DOWN;
    else if (s->heals != s_prev_heals) ev = BB_EV_PANEL_HEAL;
    else if (r != s_prev_recov) ev = BB_EV_I2C_RECOVER;
    else if (f - s_prev_fails >= 3 && now - s_last_burst >= 60000000LL) { ev = BB_EV_I2C_BURST; s_last_burst = now; }
    else if (now - s_last_beat >= (int64_t)BB_BEAT_S * 1000000) ev = BB_EV_TICK;
    if (ev >= 0 && ev != BB_EV_TICK) s_last_edge = now;
beat_now:
    s_have_prev = true; s_prev_night = s->night; s_prev_link = s->link; s_prev_heals = s->heals; s_prev_recov = r; s_prev_fails = f;
    if (ev >= 0) beat(ev, s, now);
}

/* ---- the printout (director `blackbox`) ---- */
static const char *rst_name(int r) {
    static const char *const N[] = { "UNKNOWN", "POWER-ON", "EXTERNAL", "SOFTWARE (asked for)", "PANIC", "INTERRUPT WATCHDOG", "TASK WATCHDOG",
                                     "OTHER WATCHDOG (the RTC watchdog)", "DEEP-SLEEP WAKE", "BROWNOUT", "SDIO", "USB", "JTAG", "EFUSE", "POWER GLITCH", "CPU LOCKUP" };
    return r >= 0 && r < (int)(sizeof N / sizeof N[0]) ? N[r] : "?";
}
static const char *rom_name(int r) {
    switch (r) {
    case 0x01: return "power-on"; case 0x03: return "software"; case 0x05: return "deep sleep"; case 0x07: return "main watchdog 0";
    case 0x08: return "main watchdog 1"; case 0x09: return "RTC WATCHDOG (system)"; case 0x0B: return "main watchdog 0 (CPU)"; case 0x0C: return "software (CPU)";
    case 0x0D: return "RTC WATCHDOG (CPU)"; case 0x0F: return "BROWNOUT"; case 0x10: return "RTC WATCHDOG (system + RTC)"; case 0x11: return "main watchdog 1 (CPU)";
    case 0x12: return "super watchdog"; case 0x13: return "clock glitch"; case 0x14: return "efuse"; case 0x15: return "USB UART"; case 0x16: return "USB JTAG";
    case 0x17: return "power glitch"; default: return "?";
    }
}
static const char *wake_name(int w) {
    switch (w) { case 0: return "none"; case 2: return "ext0"; case 3: return "ext1 (PWR)"; case 4: return "timer"; case 7: return "gpio"; default: return "other"; }
}
static void dur(uint32_t s, char *b, size_t n) {
    if (s >= 3600) snprintf(b, n, "%luh%02lum", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    else snprintf(b, n, "%lum%02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
}
static void wall(uint32_t t, char *b, size_t n) {       /* UTC, without newlib's strftime (~4 KB of app): Howard Hinnant's civil_from_days */
    if (!t) { snprintf(b, n, "clock unknown"); return; }
    const uint32_t sec = t % 86400; int32_t z = (int32_t)(t / 86400) + 719468;
    const int32_t era = z / 146097, doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    snprintf(b, n, "%04hu-%02hu-%02hu %02hu:%02hu UTC", (unsigned short)(yoe + era * 400 + (m <= 2)), (unsigned short)m, (unsigned short)d,
             (unsigned short)(sec / 3600), (unsigned short)(sec / 60 % 60));   /* (short: the compiler can bound the text) */
}
static void print_beat(const bb_beat_t *b, const char *lead) {
    char u[16], w[48];
    dur(b->up_s, u, sizeof u); wall(b->wall, w, sizeof w);
    ESP_LOGI(TAG, "%sboot %u up %s, %s | %s | %s%s%s%s%s%s%s| battery %s%u%% %u mV | heap %u KB | I2C %u failed (last 0x%02x), %u recoveries | %u fps | rail heals %u",
             lead, b->boot_seq, u, w, bb_event_name(b->event),
             b->state & BB_S_NIGHT ? "light OFF " : "light on ", b->state & BB_S_LINK ? "linked " : "", b->state & BB_S_GRACE ? "IN THE SLEEP'S GRACE " : "",
             b->state & BB_S_CABLE ? "on the cable " : "on battery ", b->state & BB_S_CHARGING ? "charging " : "", b->state & BB_S_NO_PANEL ? "NO PANEL " : "",
             b->state & BB_S_LOCKED ? "touch locked " : "", b->pct == 255 ? "? " : "", b->pct == 255 ? 0 : b->pct, b->mv, b->heap_kb, b->i2c_fails, b->i2c_last, b->i2c_recov, b->fps, b->heals);
}
typedef struct { uint32_t n, cap; bb_boot_t *boot; bb_beat_t *last; bool *has; } coll_t;
static void collect_boot(const void *rec, void *ctx) {
    coll_t *c = ctx; const bb_hdr_t *h = rec;
    if (h->type == BB_T_BOOT && c->n < c->cap) { memcpy(&c->boot[c->n], rec, sizeof(bb_boot_t)); c->has[c->n] = false; c->n++; }
    else if (h->type == BB_T_PANIC && c->n && c->n <= c->cap) {    /* a panic follows its boot: kept in the boot's spare room for the printout */
        const bb_panic_t *p = rec; bb_boot_t *b = &c->boot[c->n - 1];
        memcpy(b->spare, &p->pc, 4); b->spare[4] = 1;
    }
}
static void collect_beat(const void *rec, void *ctx) {
    coll_t *c = ctx; const bb_beat_t *b = rec;
    for (uint32_t i = c->n; i-- > 0;) if ((uint16_t)c->boot[i].h.seq == b->boot_seq) { c->last[i] = *b; c->has[i] = true; return; }
}
static void print_panic(const void *rec, void *ctx) {
    const bb_panic_t *p = rec; (void)ctx;
    if (p->h.type != BB_T_PANIC) return;
    char bt[8 * 12 + 1]; size_t o = 0; bt[0] = 0;
    for (int i = 0; i < p->nbt && i < 8; i++) o += (size_t)snprintf(bt + o, sizeof bt - o, " 0x%08lx", (unsigned long)p->bt[i]);
    ESP_LOGW(TAG, "  PANIC (record %lu): core %u, task '%.11s', exception %u, cause %u, PC 0x%08lx, address 0x%08lx | backtrace%s  (decode: xtensa-esp32s3-elf-addr2line -pfiaC -e pocket_tank.elf <addresses>)",
             (unsigned long)p->h.seq, p->core, p->task, p->exception, p->exccause, (unsigned long)p->pc, (unsigned long)p->excvaddr, bt);
}
static void print_beat_cb(const void *rec, void *ctx) { (void)ctx; print_beat(rec, "  "); }
void blackbox_print(bool all) {
    if (!s_on) { ESP_LOGW(TAG, "no black box on this board (the flash gap would not register)"); return; }
    ESP_LOGI(TAG, "THE BLACK BOX (flash 0x%X, %u KB): %lu boot records of %u, %lu heartbeats of %u | this boot %lu | RTC watchdog %s, %d s | %s",
             (unsigned)BB_FLASH_BASE, (unsigned)(BB_FLASH_SIZE / 1024), (unsigned long)s_boots.count, (unsigned)(BB_BOOT_SECTORS * BB_SECTOR / BB_BOOT_SLOT),
             (unsigned long)s_beats.count, (unsigned)(BB_BEAT_SECTORS * BB_SECTOR / BB_BEAT_SLOT), (unsigned long)s_boot_seq, s_rwdt_on ? "ON" : "off", BB_RWDT_S,
             all ? "everything kept" : "the last 12 boots and 24 heartbeats (`blackbox all`: everything)");
    coll_t c = { .cap = BB_BOOT_SECTORS * BB_SECTOR / BB_BOOT_SLOT };
    c.boot = heap_caps_malloc(c.cap * sizeof(bb_boot_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    c.last = heap_caps_malloc(c.cap * sizeof(bb_beat_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    c.has = heap_caps_malloc(c.cap * sizeof(bool), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c.boot || !c.last || !c.has) { ESP_LOGW(TAG, "no PSRAM for the printout"); goto out; }
    bb_ring_walk(&s_boots, 0, collect_boot, &c);
    bb_ring_walk(&s_beats, 0, collect_beat, &c);
    for (uint32_t i = all || c.n <= 12 ? 0 : c.n - 12; i < c.n; i++) {
        const bb_boot_t *b = &c.boot[i];
        char on[96], off[96], up[16];
        bb_pmic_on_src(b->pmic[0], on, sizeof on); bb_pmic_off_src(b->pmic[1], off, sizeof off); dur(b->crumb_up_s, up, sizeof up);
        ESP_LOGI(TAG, "BOOT %lu [%.8s %08lx]: reset %s (ROM: %s / %s), wake %s | board found %s%s%s | %s",
                 (unsigned long)b->h.seq, b->ver, (unsigned long)__builtin_bswap32(b->build), rst_name(b->reset), rom_name(b->rom0), rom_name(b->rom1), wake_name(b->wake),
                 b->tries == 1 ? "at once" : b->tries ? "after retries" : "NEVER", b->flags & BB_BF_BUS_STUCK ? (b->flags & BB_BF_BUS_FREED ? " | the I2C bus was STUCK, freed" : " | the I2C bus was STUCK, NOT freed") : "",
                 b->flags & BB_BF_NO_PANEL ? " | NO PANEL: restarted" : "", b->spare[4] ? "A PANIC RECORD FOLLOWS" : "");
        if (b->flags & BB_BF_CRUMB)
            ESP_LOGI(TAG, "  the run before it was %s, up %s, %lu frames, %u I2C failures%s", bb_phase_name(b->crumb_phase), up, (unsigned long)b->crumb_frames,
                     b->crumb_i2c_fails, b->crumb_i2c ? " - STOPPED INSIDE an I2C transaction" : "");
        else ESP_LOGI(TAG, "  the run before it left no breadcrumb: power was lost (a power-on, a PWR-held cut, a flat battery, a pulled cable)");
        if (b->crumb_i2c) ESP_LOGW(TAG, "  ... to the chip at 0x%02x", b->crumb_i2c);
        if (b->flags & BB_BF_PMIC)
            ESP_LOGI(TAG, "  PMIC at boot: power-on source %02x (%s), power-off source %02x (%s) | 10=%02x 18=%02x 19=%02x 22=%02x 23=%02x 24=%02x 27=%02x | IRQ 48=%02x 49=%02x 4A=%02x | 80=%02x 90=%02x 91=%02x 41=%02x%s",
                     b->pmic[0], on, b->pmic[1], off, b->pmic[2], b->pmic[3], b->pmic[4], b->pmic[5], b->pmic[6], b->pmic[7], b->pmic[8], b->pmic[9], b->pmic[10],
                     b->pmic[11], b->pmic[12], b->pmic[13], b->pmic[14], b->pmic[15], b->pmic_ok == (1u << BB_PMIC_N) - 1 ? "" : " (some not read)");
        else ESP_LOGI(TAG, "  PMIC at boot: not read");
        if (c.has[i]) print_beat(&c.last[i], "  its last heartbeat: ");
        else ESP_LOGI(TAG, "  its last heartbeat: none kept");
    }
    bb_ring_walk(&s_boots, 0, print_panic, NULL);
    ESP_LOGI(TAG, "heartbeats, oldest first:");
    bb_ring_walk(&s_beats, all ? 0 : 24, print_beat_cb, NULL);
out:
    free(c.boot); free(c.last); free(c.has);
    i2c_guard_report();
}

/* the dev tests (director `blackbox test ...`): the chip restarts, the next boot's record shows it */
void blackbox_test(const char *what) {
    if (!strcmp(what, "rwdt")) {
        ESP_LOGW(TAG, "test: a light sleep that never wakes (10 min timer only, nothing else) - the RTC watchdog should restart the chip in %d s", BB_RWDT_S);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_sleep_enable_timer_wakeup(600LL * 1000000);
        blackbox_crumb_phase(BB_PH_LIGHT_SLEEP);
        esp_light_sleep_start();
        ESP_LOGE(TAG, "test: woke from the never-ending sleep: the RTC watchdog did NOT fire");
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    } else if (!strcmp(what, "panic")) {
        ESP_LOGW(TAG, "test: a panic on purpose - the next boot writes its record");
        vTaskDelay(pdMS_TO_TICKS(100));
        abort();
    } else ESP_LOGW(TAG, "blackbox test rwdt | blackbox test panic");
}
