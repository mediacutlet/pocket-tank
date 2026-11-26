/* display_p4_beta.c - BETA (2026-11-22): the panel bring-up for the Waveshare
 * ESP32-P4-WIFI6-Touch-LCD-7 (TANK_P4_LCD7) and -LCD-8 (TANK_P4_LCD8), the 10.1's
 * siblings. Included by display_port_jd9365.c under TANK_P4_BETA; everything
 * after the panel (the PPA's turn, the tiles, the two DSI buffers) is the 10.1's.
 *
 * NOT SEEN ON A BOARD: nobody here owns either. Every value below is Waveshare's,
 * cited in docs/board-p4-7-8-beta.md (file and line); the method was checked by
 * reading the 10.1's table out of the same BSP and finding it identical, command
 * for command, to the one display_port_jd9365.c runs on the bench.
 *
 * SAFE FAILURE. The likeliest fault is a dark glass, so nothing here may stop the
 * boot. In ESP-IDF 5.5 a panel that never answers can hang it two ways: the DSI
 * read waits forever (components/hal/mipi_dsi_hal.c,
 * mipi_dsi_hal_host_gen_read_short_packet), and since the bus turns every timeout
 * off (esp_lcd_mipi_dsi_bus.c 121) and the command channel asks for an
 * acknowledgement on every command (esp_lcd_panel_io_dbi.c 38), a stalled link
 * fills the command FIFO and the next write spins forever too (mipi_dsi_hal.c 173,
 * 186-194) - tx_param itself always returns ESP_OK. Both Waveshare's ILI9881C
 * driver and Espressif's JD9365 driver read the id that way in their init. So:
 * neither driver is used (their steps are sent here in their order, cited); the id
 * is read with a deadline (beta_dsi_read); a failed read turns the acknowledgements
 * off; and EVERY command first waits, with a deadline, for the link to be idle
 * (beta_link_idle) - an idle link takes one short packet without the HAL ever
 * waiting. A stall ends the bring-up: false, the reset line released. The board
 * goes on without a picture - the console answers on USB, the sound plays, touch
 * works if its chip does - and the boot log says where it stopped.
 *
 * NOT the -7B (1024 x 600, another board): nothing here is for it. */
#include "hal/mipi_dsi_host_ll.h"
#include "version.h"
#include "esp_rom_sys.h"
#include <stdio.h>

typedef struct { uint8_t cmd; const uint8_t *data; uint8_t n; uint16_t ms; } beta_cmd_t;
static uint8_t s_beta_id[3]; static int s_beta_id_n = -1;   /* the panel's id read back: -1 not asked, 0 no answer */
static const char *s_beta_step = "not started";              /* the last bring-up step reached (the Tank Check's line) */

static bool s_beta_stalled;                                  /* the link stopped taking commands: nothing more is sent */
/* the command and payload FIFOs empty and no read pending, within `ms`: then one short packet goes without the HAL waiting */
static bool beta_link_idle(int ms) {
    dsi_host_dev_t *h = &MIPI_DSI_HOST;
    const int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (!mipi_dsi_host_ll_gen_is_cmd_fifo_empty(h) || !mipi_dsi_host_ll_gen_is_write_fifo_empty(h) || mipi_dsi_host_ll_gen_is_read_cmd_busy(h)) {
        if (esp_timer_get_time() > end) return false;
        esp_rom_delay_us(10);
    }
    return true;
}
/* one DCS read, as the HAL does it but with a deadline: 0 = no answer in `ms` (and then no more acknowledgements asked) */
static int beta_dsi_read(uint8_t dcs, uint8_t *out, int n, int ms) {
    dsi_host_dev_t *h = &MIPI_DSI_HOST;
    if (s_beta_stalled || !beta_link_idle(ms)) { s_beta_stalled = true; return 0; }
    const int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (mipi_dsi_host_ll_gen_is_cmd_fifo_full(h)) if (esp_timer_get_time() > end) return 0;
    mipi_dsi_host_ll_gen_set_packet_header(h, 0, MIPI_DSI_DT_SET_MAXIMUM_RETURN_PKT, (uint8_t)(n >> 8), (uint8_t)n);
    mipi_dsi_host_ll_enable_video_mode(h, false);
    mipi_dsi_host_ll_enable_bta(h, true);
    mipi_dsi_host_ll_gen_set_rx_vcid(h, 0);
    while (mipi_dsi_host_ll_gen_is_cmd_fifo_full(h)) if (esp_timer_get_time() > end) return 0;
    mipi_dsi_host_ll_gen_set_packet_header(h, 0, MIPI_DSI_DT_DCS_READ_0, 0, dcs);
    while (mipi_dsi_host_ll_gen_is_read_cmd_busy(h) || mipi_dsi_host_ll_gen_is_read_fifo_empty(h)) {
        if (esp_timer_get_time() > end) {                    /* no answer: ask for no more turnarounds (the table may still go, blind) */
            mipi_dsi_host_ll_enable_cmd_ack(h, false); mipi_dsi_host_ll_enable_bta(h, false);
            return 0;
        }
    }
    int got = 0;
    while (!mipi_dsi_host_ll_gen_is_read_fifo_empty(h)) {
        uint32_t w = mipi_dsi_host_ll_gen_read_payload_fifo(h);
        for (int i = 0; i < 4; i++) if (got < n) out[got++] = (uint8_t)(w >> (8 * i));
    }
    return got;
}
/* one command, only onto an idle link (tx_param always says ESP_OK: the link's state is the only check there is) */
static bool beta_tx(uint8_t cmd, const uint8_t *d, int n) {
    if (s_beta_stalled) return false;
    if (!s_io || !beta_link_idle(100)) { s_beta_stalled = true; ESP_LOGE(TAG, "beta: the DSI link stopped taking commands (at %02x): nothing more is sent", cmd); return false; }
    return esp_lcd_panel_io_tx_param(s_io, cmd, n ? d : NULL, n) == ESP_OK;
}
static bool beta_send(const beta_cmd_t *c, int count) {
    for (int i = 0; i < count; i++) {
        if (!beta_tx(c[i].cmd, c[i].data, c[i].n)) { ESP_LOGE(TAG, "beta: init command %d of %d (%02x) not sent", i, count, c[i].cmd); return false; }
        if (c[i].ms) vTaskDelay(pdMS_TO_TICKS(c[i].ms));
    }
    return true;
}
#define BETA_TRY(x, step) do { s_beta_step = step; esp_err_t _e = (x); if (_e != ESP_OK) { ESP_LOGE(TAG, "beta: %s: %s", step, esp_err_to_name(_e)); return false; } } while (0)

#ifdef TANK_P4_LCD7
/* ---- the 7 inch: Waveshare's BSP 2.0.3 esp32_p4_wifi6_touch_lcd_x.c 464-670 (ili9881c_vendor_specific_init_default),
 * generated from it; comments and the entries Waveshare left commented out dropped. 197 commands. ---- */
#include "display_p4_lcd7_table.h"
/* the panel controller at 0x45 (Waveshare's esp_lcd_ili9881c 2.0.0, esp_lcd_ili9881c.c 124-151, which writes it before
 * the panel starts; the register names are Waveshare's panel-regulator driver's: 0x95 LCD power bits, 0x96 PWM) */
static i2c_master_dev_handle_t s_mcu;
static bool s_beta_mcu; static uint8_t s_beta_mcu_id, s_beta_mcu_size, s_beta_mcu_ver;   /* (the 7 inch's panel controller) */
static bool mcu_w(uint8_t reg, uint8_t v) { uint8_t b[2] = { reg, v }; return s_mcu && i2c_master_transmit(s_mcu, b, 2, 50) == ESP_OK; }
static bool mcu_r(uint8_t reg, uint8_t *v) {          /* write the register, stop, wait, then read (as Waveshare's Linux driver reads it) */
    if (!s_mcu || i2c_master_transmit(s_mcu, &reg, 1, 50) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    return i2c_master_receive(s_mcu, v, 1, 50) == ESP_OK;
}
static void lcd7_mcu_backlight(uint8_t level) { if (s_beta_mcu) mcu_w(0x96, level); }
static void lcd7_mcu_up(void) {
    if (i2c_master_probe(s_i2c, I2C_ADDR_LCD7_MCU, 50) != ESP_OK) { ESP_LOGE(TAG, "beta: no panel controller at 0x45 (the 7 inch's): its power may stay off"); return; }
    i2c_device_config_t c = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_LCD7_MCU, .scl_speed_hz = 100000 };
    if (i2c_master_bus_add_device(s_i2c, &c, &s_mcu) != ESP_OK) { s_mcu = NULL; return; }
    s_beta_mcu = true;
    mcu_r(0x98, &s_beta_mcu_id); mcu_r(0x97, &s_beta_mcu_size); mcu_r(0x99, &s_beta_mcu_ver);
    ESP_LOGI(TAG, "beta: panel controller at 0x45: id 0x%02x, size %d, version 0x%02x", s_beta_mcu_id, s_beta_mcu_size, s_beta_mcu_ver);
    mcu_w(0x95, 0x11); mcu_w(0x95, 0x17);            /* Waveshare's order and values */
    mcu_w(0x96, 0x00); vTaskDelay(pdMS_TO_TICKS(100));
    mcu_w(0x96, 0xFF);
    mcu_w(0x96, 0x00);                               /* (ours: dark until the first frame lands - backlight() lights it) */
}
static bool beta_panel_steps(void) {
    /* Waveshare's order (BSP 1081-1102, then esp_lcd_ili9881c.c new_panel 124-153): the DSI PHY's power, the bus, the command
       channel; then the reset line as an output (low), the panel controller and 1000 ms; then the DPI panel */
    esp_ldo_channel_config_t ldo = { .chan_id = P4_DSI_LDO_CHAN, .voltage_mv = P4_DSI_LDO_MV };
    BETA_TRY(esp_ldo_acquire_channel(&ldo, &s_ldo), "DSI power");
    esp_lcd_dsi_bus_config_t dsi = { .bus_id = 0, .num_data_lanes = P4_DSI_LANES, .phy_clk_src = 0, .lane_bit_rate_mbps = P4_DSI_MBPS };
    BETA_TRY(esp_lcd_new_dsi_bus(&dsi, &s_dsi), "DSI bus");
    esp_lcd_dbi_io_config_t dbi = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    BETA_TRY(esp_lcd_new_panel_io_dbi(s_dsi, &dbi, &s_io), "DSI command channel");
    s_beta_step = "reset line";
    gpio_set_level(P4_PIN_LCD_RST, 0);
    gpio_config_t rst = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << P4_PIN_LCD_RST };
    gpio_config(&rst);
    s_beta_step = "panel controller"; lcd7_mcu_up();
    vTaskDelay(pdMS_TO_TICKS(1000));                 /* Waveshare's wait after it (esp_lcd_ili9881c.c 151) */
    esp_lcd_dpi_panel_config_t dpi = {          /* the BSP's 7 inch timing (esp32_p4_wifi6_touch_lcd_x.c 1148-1174) */
        .virtual_channel = 0, .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT, .dpi_clock_freq_mhz = 80,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565, .num_fbs = 2,
        .video_timing = { .h_size = P4_PANEL_W, .v_size = P4_PANEL_H,
                          .hsync_pulse_width = 50, .hsync_back_porch = 239, .hsync_front_porch = 33,
                          .vsync_pulse_width = 30, .vsync_back_porch = 20, .vsync_front_porch = 2 },
    };
    dpi.flags.use_dma2d = false;                /* the PPA writes the buffers, the DMA scans them (as on the 10.1) */
    dpi.flags.disable_lp = false;               /* Waveshare's default (the 10.1 holds high speed; the Tab5's ST7121 sheared so) */
    esp_lcd_panel_handle_t p = NULL;
    BETA_TRY(esp_lcd_new_panel_dpi(s_dsi, &dpi, &p), "DPI panel");
    s_beta_step = "panel reset";                /* the driver's reset (esp_lcd_ili9881c.c panel_ili9881c_reset): low 10 ms, high 10 ms */
    gpio_set_level(P4_PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(P4_PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
    /* the driver's init (panel_ili9881c_init, esp_lcd_ili9881c.c 413-476): page 1, the id (registers 0..2, 438-444), 2 lanes,
       page 0, sleep out, MADCTL, COLMOD, the table */
    s_beta_step = "id read";
    if (!beta_tx(0xFF, (const uint8_t[]){ 0x98, 0x81, 0x01 }, 3)) return false;
    s_beta_id_n = 0;
    for (int i = 0; i < 3; i++) { uint8_t v = 0; if (beta_dsi_read((uint8_t)i, &v, 1, 50) == 1) { s_beta_id[i] = v; s_beta_id_n++; } }
    if (s_beta_id_n == 3) ESP_LOGI(TAG, "beta: ILI9881C id %02x %02x %02x", s_beta_id[0], s_beta_id[1], s_beta_id[2]);
    else ESP_LOGE(TAG, "beta: the panel did not answer its id read (%d of 3 bytes) - going on with the table", s_beta_id_n);
    s_beta_step = "init table";
    if (!beta_tx(0xB7, (const uint8_t[]){ 0x03 }, 1)) return false;        /* ILI9881C_PAD_CONTROL: two lanes */
    if (!beta_tx(0xFF, (const uint8_t[]){ 0x98, 0x81, 0x00 }, 3)) return false;
    if (!beta_tx(0x11, NULL, 0)) return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!beta_tx(0x36, (const uint8_t[]){ 0x00 }, 1)) return false;        /* MADCTL: RGB order */
    if (!beta_tx(0x3A, (const uint8_t[]){ 0x55 }, 1)) return false;        /* COLMOD for 16 bpp (the table sets 0x77 after, as Waveshare runs it) */
    if (!beta_send(lcd7_init_cmds, sizeof lcd7_init_cmds / sizeof lcd7_init_cmds[0])) return false;   /* (ends with sleep out + display on) */
    BETA_TRY(esp_lcd_panel_init(p), "DPI start");
    s_panel = p; s_beta_step = "up";
    return true;
}
#endif

#ifdef TANK_P4_LCD8
/* ---- the 8 inch: Waveshare's BSP 2.0.3 esp32_p4_wifi6_touch_lcd_x.c 44-237 (jd9365_vendor_specific_init_default, the
 * CONFIG_BSP_LCD_TYPE_800_1280_8_INCH branch), generated from it. 174 commands. ---- */
#include "display_p4_lcd8_table.h"
static bool beta_panel_steps(void) {
    gpio_set_level(P4_PIN_LCD_RST, 1);               /* (an output from the start, high: released whatever happens below) */
    gpio_config_t rst = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << P4_PIN_LCD_RST };
    gpio_config(&rst);
    esp_ldo_channel_config_t ldo = { .chan_id = P4_DSI_LDO_CHAN, .voltage_mv = P4_DSI_LDO_MV };
    BETA_TRY(esp_ldo_acquire_channel(&ldo, &s_ldo), "DSI power");
    esp_lcd_dsi_bus_config_t dsi = { .bus_id = 0, .num_data_lanes = P4_DSI_LANES, .phy_clk_src = 0, .lane_bit_rate_mbps = P4_DSI_MBPS };
    BETA_TRY(esp_lcd_new_dsi_bus(&dsi, &s_dsi), "DSI bus");
    esp_lcd_dbi_io_config_t dbi = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    BETA_TRY(esp_lcd_new_panel_io_dbi(s_dsi, &dbi, &s_io), "DSI command channel");
    esp_lcd_dpi_panel_config_t dpi = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);   /* the BSP's for the 8 and the 10.1 */
    dpi.num_fbs = 2;
    dpi.flags.use_dma2d = false;                /* as the 10.1 */
    dpi.flags.disable_lp = true;                /* as the 10.1 (the same panel family and timing) */
    esp_lcd_panel_handle_t p = NULL;
    BETA_TRY(esp_lcd_new_panel_dpi(s_dsi, &dpi, &p), "DPI panel");
    /* the JD9365 driver's reset (esp_lcd_jd9365.c panel_jd9365_reset): high 5 ms, low 10 ms, high 120 ms - the touch chip too */
    s_beta_step = "panel reset";
    gpio_set_level(P4_PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(P4_PIN_LCD_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(P4_PIN_LCD_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));
    /* the driver's init (panel_jd9365_init): the id (DCS 04, 3 bytes), page 0, MADCTL, COLMOD, two lanes, the table */
    s_beta_step = "id read";
    s_beta_id_n = beta_dsi_read(0x04, s_beta_id, 3, 50);
    if (s_beta_id_n == 3) ESP_LOGI(TAG, "beta: JD9365 id %02x %02x %02x", s_beta_id[0], s_beta_id[1], s_beta_id[2]);
    else ESP_LOGE(TAG, "beta: the panel did not answer its id read (%d of 3 bytes) - going on with the table", s_beta_id_n);
    s_beta_step = "init table";
    if (!beta_tx(0xE0, (const uint8_t[]){ 0x00 }, 1)) return false;        /* JD9365_CMD_PAGE, PAGE_USER */
    if (!beta_tx(0x36, (const uint8_t[]){ 0x00 }, 1)) return false;        /* MADCTL: RGB order */
    if (!beta_tx(0x3A, (const uint8_t[]){ 0x55 }, 1)) return false;        /* COLMOD: 16 bpp */
    if (!beta_tx(0x80, (const uint8_t[]){ 0x01 }, 1)) return false;        /* JD9365_CMD_DSI_INT0: two lanes */
    if (!beta_send(lcd8_init_cmds, sizeof lcd8_init_cmds / sizeof lcd8_init_cmds[0])) return false;   /* (ends with sleep out + display on) */
    BETA_TRY(esp_lcd_panel_init(p), "DPI start");
    s_panel = p; s_beta_step = "up";
    return true;
}
#endif

/* the bring-up, and on any failure the shared reset line let go: the touch chip hangs on it too */
static bool beta_panel_up(void) {
    if (beta_panel_steps()) return true;
    gpio_set_level(P4_PIN_LCD_RST, 1);
    return false;
}
/* the boot log's board line (opening the USB port restarts these boards, so a Tank Check reads a fresh one) */
static void beta_board_line(void) {
    char id[24];
    if (s_beta_id_n == 3) snprintf(id, sizeof id, "%02x %02x %02x", s_beta_id[0], s_beta_id[1], s_beta_id[2]);
    else snprintf(id, sizeof id, "NO ANSWER (%d)", s_beta_id_n);
    ESP_LOGI(TAG, "board: BETA %s (%s %dx%d over %d DSI lanes at %d Mbps) - panel %s, id %s%s, touch %s, codec %s - NOT TRIED ON A BOARD BY US",
             PT_BOARD_NAME,
#ifdef TANK_P4_LCD7
             "ILI9881C",
#else
             "JD9365",
#endif
             P4_PANEL_W, P4_PANEL_H, P4_DSI_LANES, P4_DSI_MBPS, s_panel ? "UP" : "DOWN", id,
#ifdef TANK_P4_LCD7
             s_beta_mcu ? ", panel controller 0x45 answered" : ", panel controller 0x45 SILENT",
#else
             "",
#endif
             s_gt911 ? "answered" : "SILENT", s_codec ? "answered" : "silent");
    if (!s_panel) ESP_LOGE(TAG, "board: no picture - the bring-up stopped at \"%s\". The tank runs on without it: send a Tank Check", s_beta_step);
}
