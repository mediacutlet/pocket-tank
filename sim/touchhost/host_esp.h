/* host_esp.h - just enough of ESP-IDF for the firmware's touch port (firmware/main/touch_port_ft3168.c) to build on
 * the host, so the sim's --selftest-touch-port runs the real tap routing (sim/selftest_touch.c, 0.4.1). The chips'
 * reports, the clock and the board are the test's (selftest_touch.c defines every function here). */
#ifndef HOST_ESP_H
#define HOST_ESP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
int64_t esp_timer_get_time(void);
void host_log(const char *tag, const char *fmt, ...);
#define ESP_LOGI(tag, ...) host_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) host_log(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) host_log(tag, __VA_ARGS__)
/* I2C */
typedef struct host_i2c_bus *i2c_master_bus_handle_t;
typedef struct host_i2c_dev *i2c_master_dev_handle_t;
enum { I2C_ADDR_BIT_LEN_7 = 0 };
typedef struct { int dev_addr_length; uint16_t device_address; uint32_t scl_speed_hz; struct { uint32_t disable_ack_check : 1; } flags; } i2c_device_config_t;
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t addr, int timeout_ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *dev);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wn, int timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wn, uint8_t *r, size_t rn, int timeout_ms);
/* GPIO */
typedef int gpio_num_t;
typedef enum { GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2 } gpio_mode_t;
typedef struct { uint64_t pin_bit_mask; gpio_mode_t mode; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *c);
esp_err_t gpio_sleep_sel_dis(gpio_num_t pin);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
/* FreeRTOS */
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(int ticks);
/* esp_lcd's touch (the 1.8's CST816 / FT3168 drivers) */
typedef struct host_tp *esp_lcd_touch_handle_t;
typedef struct host_io *esp_lcd_panel_io_handle_t;
typedef struct { uint32_t dev_addr; uint32_t scl_speed_hz; } esp_lcd_panel_io_i2c_config_t;
#define ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG() { .dev_addr = 0x15 }
#define ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG() { .dev_addr = 0x38 }
typedef struct { uint16_t x_max, y_max; int rst_gpio_num, int_gpio_num;
                 struct { unsigned reset, interrupt; } levels; struct { unsigned swap_xy, mirror_x, mirror_y; } flags; } esp_lcd_touch_config_t;
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t bus, const esp_lcd_panel_io_i2c_config_t *cfg, esp_lcd_panel_io_handle_t *io);
esp_err_t esp_lcd_touch_new_i2c_cst816s(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *cfg, esp_lcd_touch_handle_t *tp);
esp_err_t esp_lcd_touch_new_i2c_ft5x06(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *cfg, esp_lcd_touch_handle_t *tp);
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);
bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *n, uint8_t max);
#endif
