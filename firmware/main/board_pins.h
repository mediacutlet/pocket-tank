/* board_pins.h — Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1: SH8601 + FT3168;
 * V2: CO5300 + CST816). Sources: Waveshare esp-idf examples and the official
 * Arduino variant. VERIFY I2C SDA/SCL on the bench: Waveshare's own code says
 * SDA=15/SCL=14, the Arduino variant says the reverse. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "sdkconfig.h"

#if CONFIG_POCKET_TANK_BOARD_CYD28
/* The 2.8" ESP32-S3 CYD, ES3C28P (lcdwiki.com, ES3C28P_ES2N28P_Specification_V1.0
 * section 4.2). A stock board, no modifications: everything below is as the
 * vendor wires it. The LCD's reset is CHIP_PU - it resets with the chip, so
 * there is no reset pin to drive. */
#define PIN_LCD_CS        10
#define PIN_LCD_DC        46       /* high = data, low = command */
#define PIN_LCD_SCLK      12
#define PIN_LCD_MOSI      11
#define PIN_LCD_MISO      13
#define PIN_LCD_BL        45       /* high = backlight on; PWM for brightness */
#define PIN_I2C_SDA       16       /* shared: touch, audio codec, the I2C socket */
#define PIN_I2C_SCL       15
#define PIN_TP_RST        18       /* low = reset */
#define PIN_TP_INT        17       /* low while touched (the port polls instead) */
#define I2C_ADDR_FT6336   0x38
#define PANEL_W           240      /* native portrait; the panel scans landscape (MADCTL) */
#define PANEL_H           320
/* audio: the ES8311 (I2C 0x18) on I2S, and the power amplifier's enable */
#define PIN_I2S_MCLK      4
#define PIN_I2S_BCLK      5
#define PIN_I2S_WS        7
#define PIN_I2S_DOUT      8        /* ESP -> codec DSDIN; GPIO6 is the microphone's way back, unused */
#define PIN_AMP_EN        1
#define AMP_EN_ON         0        /* the spec: "low level enable" */
#else
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
/* audio (resources/ESP32-S3-Touch-AMOLED-1.8.pdf): the ES8311 on I2S, the NS4150B's CTRL */
#define PIN_I2S_MCLK      16
#define PIN_I2S_BCLK      9
#define PIN_I2S_WS        45
#define PIN_I2S_DOUT      8        /* ESP -> codec DSDIN */
#define PIN_AMP_EN        46       /* NS4150B CTRL, 10k pulldown on the board */
#define AMP_EN_ON         1
#endif  /* board */
#endif
