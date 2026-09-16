/* display_port_st7703.c — Waveshare ESP32-P4-WIFI6-Touch-LCD-4B: 720x720
 * square 2-lane MIPI-DSI (ST7703) via the 4B BSP. The tank renders landscape
 * 448x368 (common/render.c); the panel shows it SCALED TO FILL the whole
 * 720x720 square glass (no letterbox; aspect stretched to the square - the
 * glass geometry lives in display_port.h; the touch port maps through the
 * same).
 *
 * The BSP's DPI panel driver allocates its OWN PSRAM framebuffers (triple,
 * RGB565) and, for a foreign user buffer, copies it in via the AHB-GDMA DMA2D
 * path — which CANNOT address PSRAM, so every frame logged "AHB GDMA can only
 * access SRAM" and fell back to a CPU copy. Drawing DIRECTLY into the
 * driver's framebuffer takes the driver's no-copy fast path (cache write-back
 * only): no DMA2D, no GDMA storm, and we free the 1 MB buffer we used to keep.
 *
 * The BSP owns the I2C bus (I2C_NUM=1, SDA=GPIO7, SCL=GPIO8) - board_i2c_bus()
 * hands it out to the codec/battery/imu/rtc ports; there is no V1/V2 revision
 * on this board. */
#include "display_port.h"
#include "tank.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "st7703-4b";
#define S_NFBS 3                     /* BSP CONFIG_BSP_LCD_DPI_BUFFER_NUMS */

static esp_lcd_panel_handle_t s_panel;
static void *s_fb[S_NFBS];          /* the DPI driver's own PSRAM framebuffers */
static int s_fb_idx = 0;           /* rotate through them (triple-buffered) */
static uint8_t s_brightness = 0xFF; /* what bsp_display_brightness_set was last told */
static bool s_inverted;            /* 180-degree flip, done in the blit */

void display_port_set_inverted(bool inverted) { s_inverted = inverted; }

i2c_master_bus_handle_t board_i2c_bus(void) {
    if (bsp_i2c_init() != ESP_OK) return NULL;      /* idempotent: the BSP guards it internally */
    return bsp_i2c_get_handle();
}
bool board_is_v2(void) { return false; }          /* the 4B has no board revisions */

bool display_port_init(void) {
    bsp_lcd_handles_t handles;
    if (bsp_display_new_with_handles(NULL, &handles) != ESP_OK) {
        ESP_LOGE(TAG, "BSP display init failed");
        return false;
    }
    s_panel = handles.panel;
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    /* draw into the DPI driver's OWN PSRAM framebuffers (the no-copy path) */
    esp_err_t err = esp_lcd_dpi_panel_get_frame_buffer(s_panel, S_NFBS, &s_fb[0], &s_fb[1], &s_fb[2]);
    if (err != ESP_OK || !s_fb[0]) {
        ESP_LOGW(TAG, "dpi get_frame_buffer: %s (falling back is not possible)", esp_err_to_name(err));
        return false;
    }
    s_brightness = 0xFF;
    ESP_LOGI(TAG, "ST7703 %dx%d up: tank %dx%d scaled to %dx%d at (%d,%d), %d PSRAM fbs",
             PANEL_W, PANEL_H, TANK_W, TANK_H, SCALED_W, SCALED_H, GLASS_X_OFF, GLASS_Y_OFF, S_NFBS);
    return true;
}

/* scaled nearest-neighbor blit of the 448x368 tank into the FULL 720x720
 * glass (aspect stretched to the square; 180-degree when inverted). */
void display_port_flush(const uint16_t *fb) {
    if (!s_panel || !s_fb[0]) return;
    uint16_t *dst = s_fb[s_fb_idx];
    s_fb_idx = (s_fb_idx + 1) % S_NFBS;
    if (s_inverted) {
        /* 180-degree: the tank region lands mirrored, and its pixels mirror too */
        int inv_x0 = PANEL_W - GLASS_X_OFF - SCALED_W;    /* 0 (full glass) */
        int inv_y0 = PANEL_H - GLASS_Y_OFF - SCALED_H;    /* 0 (full glass) */
        for (int ry = 0; ry < SCALED_H; ry++) {
            int src_row = (ry * TANK_H) / SCALED_H;
            uint16_t *d = dst + (size_t)(inv_y0 + (SCALED_H - 1 - ry)) * PANEL_W + inv_x0;
            for (int cx = 0; cx < SCALED_W; cx++)
                d[cx] = fb[src_row * TANK_W + ((SCALED_W - 1 - cx) * TANK_W) / SCALED_W];
        }
    } else {
        for (int ry = 0; ry < SCALED_H; ry++) {
            int src_row = (ry * TANK_H) / SCALED_H;
            uint16_t *d = dst + (size_t)(GLASS_Y_OFF + ry) * PANEL_W + GLASS_X_OFF;
            for (int cx = 0; cx < SCALED_W; cx++)
                d[cx] = fb[src_row * TANK_W + (cx * TANK_W) / SCALED_W];
        }
    }
    /* the buffer IS a driver framebuffer -> the DPI driver takes its no-copy
     * cache-write-back fast path (no DMA2D / GDMA). Push the whole panel. */
    esp_err_t e = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_W, PANEL_H, dst);
    if (e != ESP_OK) {
        static int logged;
        if (logged++ < 3) ESP_LOGE(TAG, "draw_bitmap: %s", esp_err_to_name(e));
    }
}

void display_port_sleep(void) { bsp_display_backlight_off(); }
void display_port_wake(void)  { bsp_display_backlight_on(); }

/* LEDC brightness on the backlight pin (GPIO 26), driven by the BSP */
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    bsp_display_brightness_set(level * 100 / 255);
}
uint8_t display_port_brightness(void) { return s_brightness; }
