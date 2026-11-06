/* board_pins.h — the boards one image runs on, told apart at boot over I2C
 * (display_port_init; SDA / SCL, the QSPI data lines and LCD_CS are shared):
 *
 * Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1: SH8601 + FT3168; V2: CO5300 +
 * CST816). Sources: Waveshare esp-idf examples and the official Arduino
 * variant. VERIFY I2C SDA/SCL on the bench: Waveshare's own code says
 * SDA=15/SCL=14, the Arduino variant says the reverse.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-1.75C (2026-10-01; ROUND 466x466 CO5300 +
 * CST9217). Sources: resources/ESP32-S3-Touch-AMOLED-1.75C/ (the schematic's
 * GPIO table) and Waveshare's BSP (waveshare/esp32_s3_touch_amoled_1_75c).
 * No IO expander - the resets are GPIOs - no RTC chip, no SD card; an ES7210
 * microphone ADC the 1.8 does not have (its I2C address is how the board is
 * recognized). PMIC, IMU, codec, amp and every audio pin: as on the 1.8.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-2.06, the WATCH (2026-10-02; 410x502
 * CO5300 + FT3168, the 1.8's panel family a size up; worn, so its own build
 * is a PORTRAIT 410x502 tank, the panel unturned). Sources: resources/ESP32-S3-Touch-AMOLED-2.06-Watch/ (schematic,
 * wiki page) and Waveshare's BSP (waveshare/esp32_s3_touch_amoled_2_06).
 * No IO expander: the resets are GPIOs 8 and 9 - the pins the 1.8 plays its
 * I2S bit clock and data on, so the AUDIO PINS differ here (audio_port) and
 * the 1.8's would hold the panel in reset. An ES7210 like the 1.75C's, and an
 * RTC chip unlike it: no expander + ES7210 + RTC = the watch. DSI_PWR_EN is
 * not a GPIO: it is pulled up to ALDO2, so that rail IS the panel's power
 * switch. PWR key sense on GPIO 10 (SYS_OUT), an SD slot (1/2/3/17) and a
 * vibration motor (GPIO 18, fed from ALDO3) the tank does not use.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-2.16, the SQUARE (2026-10-12; 480x480
 * CO5300 + CST9220, a desk box with the USB at its foot). Sources:
 * resources/ESP32-S3-Touch-AMOLED-2.16/ (schematic, wiki page) and Waveshare's
 * BSP (waveshare/esp32_s3_touch_amoled_2_16: the init sequence, the touch
 * driver = the 1.75C's CST9217 family). No expander: resets on GPIOs 39 / 40;
 * the clock on 38 like the 1.75C; an ES7210 and an RTC like the watch - the
 * same I2C signature, so it is told from the watch by its touch chip
 * (CST92xx at 0x5A, the watch's FT3168 at 0x38). The panel's 3V3 is VCC3V3
 * (no rail to cut, as the 1.75C) with DSI_PWR_EN pulled up. Audio pins as the
 * 1.8's but the master clock on GPIO 42 (16 is SYS_OUT, the PWR key sense).
 *
 * Waveshare ESP32-S3-Touch-AMOLED-1.75 (and -B, the same board in a plastic
 * case; -G adds a GPS), 2026-10-17, BETA - no board on the bench: a keeper
 * (Bryan) has the -B. Sources: resources/ESP32-S3-Touch-AMOLED-1.75/ (the
 * schematic's GPIO table) and Waveshare's HARDWARE_REFERENCE for it. The
 * 1.75C's panel (CO5300 466 round) and touch (CST9217 at 0x5A, INT 11) with
 * the clock on 38, but its resets on 39 / 40 (the square's numbers; 1 / 2 / 3
 * are an SD slot here), a TCA9554 at 0x20 like the 1.8's - P3 RTC_INT,
 * P4 SYS_OUT (the PWR key sense), P5 AXP_IRQ, P6 QMI_INT1, P7 GPS_RST, all
 * inputs: no resets on it - an RTC (PCF85063) unlike the 1.75C, an ES7210
 * on A3V3 = ALDO1 (the 1.75C's trap: off, it clamps the bus) and the audio
 * master clock on 42. Told from the 1.8 by the ES7210: expander + ES7210.
 * Before this, the board took the 1.8's path: wrong clock pin, its panel
 * reset never driven, and the 1.8's rail trim cut ALDO1 - a dark glass. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#define PIN_LCD_CS        12
#define PIN_LCD_PCLK      11
#define PIN_LCD_DATA0     4
#define PIN_LCD_DATA1     5
#define PIN_LCD_DATA2     6
#define PIN_LCD_DATA3     7
#define PIN_I2C_SDA       15
#define PIN_I2C_SCL       14
#define PIN_TP_INT        21
#define I2C_ADDR_EXPANDER 0x20     /* TCA9554: bit0 LCD_RST, bit1 DSI_PWR_EN, bit2 TOUCH_RST, bit7 SD_CS */
#define I2C_ADDR_FT3168   0x38     /* V1 touch */
#define I2C_ADDR_CST816   0x15     /* V2 touch (probe => V2 board) */
#define PANEL_W           368      /* native portrait */
#define PANEL_H           448
#define V2_PANEL_X_GAP    0x10

/* ---- the 1.75C ---- */
#define R_PIN_LCD_PCLK    38
#define R_PIN_LCD_RST     1
#define R_PIN_LCD_TE      13       /* unused */
#define R_PIN_TP_RST      2
#define R_PIN_TP_INT      11
#define R_PIN_IMU_INT1    21       /* unused */
#define R_PIN_PWR_SENSE   3        /* SYS_OUT: high while the PWR key is down (unused: the PMIC is asked, as on the 1.8) */
#define I2C_ADDR_ES7210_BASE 0x40  /* microphone ADC: 0x40-0x43 by AD1/AD0 straps; the 1.75C rev1.1 uses 0x42 */
#define I2C_ADDR_CST9217  0x5A
#define R_PANEL           466      /* round: 466 across, the corners of the square are not there */
#define R_PANEL_X_GAP     6

/* ---- the 2.06 watch ---- */
#define W_PIN_LCD_RST     8
#define W_PIN_TP_RST      9
#define W_PIN_TP_INT      38       /* unused: polled, like the others */
#define W_PIN_LCD_TE      13       /* unused */
#define W_PIN_PWR_SENSE   10       /* SYS_OUT: high while the PWR key is down */
#define W_PIN_I2S_BCLK    41
#define W_PIN_I2S_DOUT    40       /* ESP -> codec DSDIN */
#define W_PIN_MOTOR       18       /* unused (pulled off on the board) */
#define I2C_ADDR_RTC      0x51     /* PCF85063: the 1.8 and the watch have one, the 1.75C does not */
#define W_PANEL_W         410      /* native portrait */
#define W_PANEL_H         502
#define W_PANEL_X_GAP     0x16

/* ---- the 2.16 square ---- */
#define S_PIN_LCD_PCLK    38
#define S_PIN_LCD_RST     39
#define S_PIN_TP_RST      40
#define S_PIN_TP_INT      11       /* unused: polled */
#define S_PIN_LCD_TE      13       /* unused (the RTC's INT shares the number on the wiki table: TE is on the panel header) */
#define S_PIN_PWR_SENSE   16       /* SYS_OUT: high while the PWR key is down */
#define S_PIN_I2S_MCLK    42
#define S_PIN_KEY3        18       /* the user button (unused) */
#define S_PANEL           480
#define S_PANEL_X_GAP     0

/* ---- the 1.75 / 1.75-B (expander, RTC, SD slot): the 1.75C's panel otherwise ---- */
#define B_PIN_LCD_RST     39
#define B_PIN_TP_RST      40
#define B_PIN_I2S_MCLK    42
#endif
