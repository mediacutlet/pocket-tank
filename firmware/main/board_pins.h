/* board_pins.h — which Waveshare board the firmware drives.
 *   default: ESP32-S3-Touch-AMOLED-1.8 (V1: SH8601 + FT3168; V2: CO5300 + CST816)
 *   CONFIG_POCKET_TANK_BOARD_LCD169: ESP32-S3-(Touch-)LCD-1.69 (ST7789V2 240x280
 *     over SPI + CST816T), pins from Waveshare's HARDWARE_REFERENCE.md
 *     (schematic V2.1 - the "new version" with the model name printed on it). */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "sdkconfig.h"

#ifdef CONFIG_POCKET_TANK_BOARD_LCD169
#define PIN_LCD_DC        4
#define PIN_LCD_CS        5
#define PIN_LCD_SCLK      6
#define PIN_LCD_MOSI      7
#define PIN_LCD_RST       8
#define PIN_LCD_BL        15
#define PIN_I2C_SDA       11
#define PIN_I2C_SCL       10
#define PIN_TP_RST        13
#define PIN_TP_INT        14
#define PIN_BAT_ADC       1        /* B+ through 200k/100k: VBAT = 3 x pin */
#define PIN_SYS_OUT       40       /* the PWR key, low while pressed */
#define PIN_SYS_EN        41       /* high = keep the battery switched on; low = power off (on battery) */
#define PIN_BUZZER        42       /* passive buzzer; keep LOW when silent */
#define I2C_ADDR_CST816   0x15
#define I2C_ADDR_FT3168   0x38     /* not on this board; touch_port still names it */
#define LCD169_PANEL_W    240      /* native portrait */
#define LCD169_PANEL_H    280
#define LCD169_Y_GAP      20       /* the ST7789's 320-row RAM, 280 rows visible */
#define LCD169_VIEW_W     280      /* the tank on this panel: landscape, scaled */
#define LCD169_VIEW_H     240
/* touch_port: the CST816T reports in the panel's portrait frame */
#define PANEL_W           LCD169_PANEL_W
#define PANEL_H           LCD169_PANEL_H
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
#endif
#endif
