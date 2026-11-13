#include "codec_port.h"
#include "esp_log.h"

static const char *TAG = "codec";
static i2c_master_dev_handle_t s_dev; static int s_addr;

static bool wr(uint8_t reg, uint8_t val) { uint8_t b[2] = { reg, val }; return i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK; }
static bool rd(uint8_t reg, uint8_t *val) { return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100) == ESP_OK; }

void codec_port_dump(void) {
    if (!s_dev) { ESP_LOGI(TAG, "no ES8311 found"); return; }
    uint8_t r00 = 0, r01 = 0, r0d = 0, r0e = 0, r12 = 0;
    bool ok = rd(0x00, &r00) && rd(0x01, &r01) && rd(0x0D, &r0d) && rd(0x0E, &r0e) && rd(0x12, &r12);
    ESP_LOGI(TAG, "ES8311 @0x%02x %s: RESET %02x CLKMGR %02x SYS0D %02x (analog %s, vref %s, vmid %d) SYS0E %02x DAC %02x",
             s_addr, ok ? "" : "(read failed)", r00, r01, r0d,
             (r0d & 0x80) ? "down" : "ON", (r0d & 0x04) ? "ON" : "off", r0d & 3, r0e, r12);
}

bool codec_port_present(void) { return s_dev != NULL; }

/* the powered-down set: state machine off + blocks in reset, clocks off,
   analog down with vref and vmid off, DAC down (the reference suspend
   sequence, ending where 2026-09-13's measurement left it: SYS0D F8) */
static bool power_down_regs(void) {
    static const uint8_t seq[][2] = {
        { 0x32, 0x00 }, { 0x17, 0x00 }, { 0x0E, 0xFF }, { 0x12, 0x02 }, { 0x14, 0x00 }, { 0x0D, 0xFA },
        { 0x15, 0x00 }, { 0x37, 0x08 }, { 0x02, 0x10 }, { 0x00, 0x00 }, { 0x00, 0x1F }, { 0x01, 0x30 },
        { 0x01, 0x00 }, { 0x45, 0x00 }, { 0x0D, 0xF8 }, { 0x02, 0x00 },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof seq / sizeof *seq; i++) ok &= wr(seq[i][0], seq[i][1]);
    return ok;
}
void codec_port_down(void) {
    if (!s_dev) return;
    bool ok = power_down_regs();
    uint8_t r0d = 0; rd(0x0D, &r0d);
    ESP_LOGI(TAG, "ES8311 down%s (SYS0D %02x)", ok ? "" : " - a write FAILED", r0d);
}
/* the DAC path up. The register values follow Everest's reference
   sequence as carried in Espressif's es8311 driver, reduced to what the
   tank needs: slave mode, I2S 16-bit on DSDIN, MCLK/LRCK ratio 256 (the
   user guide's 12.288/48k row, which is the same ratio at 4.096/16k), DAC
   0 dB, output to the differential pins for the NS4150B; the ADC and
   microphone stay down. */
void codec_port_settled(void) { if (s_dev) wr(0x0D, 0x02); }   /* vmid: charged -> normal operation */
bool codec_port_up(void) {
    if (!s_dev) return false;
    static const uint8_t seq[][2] = {
        { 0x00, 0x1F },             /* everything in reset while the clocks are set */
        { 0x01, 0x30 },             /* MCLK in + BCLK on */
        { 0x02, 0x00 },             /* DIV_PRE 1, MULT_PRE 1: internal mclk = MCLK (4.096 MHz) */
        { 0x03, 0x10 }, { 0x04, 0x10 },   /* ADC / DAC OSR 64 */
        { 0x05, 0x00 },             /* ADC / DAC clock dividers 1: ratio 256 */
        { 0x0B, 0x00 }, { 0x0C, 0x00 },
        { 0x10, 0x1F }, { 0x11, 0x7F },
        { 0x00, 0x80 },             /* CSM power on, slave serial port */
        { 0x01, 0x3F },             /* + ADC/DAC clocks and analog clocks on */
        { 0x09, 0x0C },             /* DAC SDP: I2S, 16-bit, left slot */
        { 0x0A, 0x0C },
        { 0x0D, 0x03 },             /* analog up: bias, refs, vmid FAST charge (0x02 = normal, after the settle) */
        { 0x0E, 0x02 },
        { 0x12, 0x00 },             /* DAC up */
        { 0x13, 0x10 },             /* output stage on */
        { 0x1C, 0x6A },
        { 0x37, 0x08 },             /* DAC EQ bypass */
        { 0x32, 0xBF },             /* DAC volume 0 dB (the mixer sets levels) */
        { 0x31, 0x00 },             /* unmute */
        { 0x0F, 0x00 },             /* no low-power trade-offs while playing */
        { 0x45, 0x00 },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof seq / sizeof *seq; i++) ok &= wr(seq[i][0], seq[i][1]);
    uint8_t r0d = 0, r12 = 0; rd(0x0D, &r0d); rd(0x12, &r12);
    ESP_LOGI(TAG, "ES8311 up%s (SYS0D %02x DAC %02x)", ok ? "" : " - a write FAILED", r0d, r12);
    return ok;
}

bool codec_port_init(i2c_master_bus_handle_t bus) {
    if (!bus) return false;
    for (int a = 0x18; a <= 0x19; a++)                /* 0011 00x, x = the CE pin */
        if (i2c_master_probe(bus, a, 50) == ESP_OK) { s_addr = a; break; }
    if (!s_addr) { ESP_LOGW(TAG, "no ES8311 at 0x18/0x19"); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = s_addr, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    uint8_t before = 0; rd(0x0D, &before);
    /* full power-down: state machine off + all blocks in reset (0x00 = the
       reset default), every clock off, every analog block down with the
       reference and vmid off, DAC down */
    bool ok = power_down_regs();
    ESP_LOGI(TAG, "ES8311 @0x%02x powered down%s (SYS0D %02x -> F8)", s_addr, ok ? "" : " - a write FAILED", before);
    codec_port_dump();
    return ok;
}

/* ES7210 suspend, as Espressif's esp_codec_dev driver does it: the four
   microphone inputs and both bias pairs off, the analog block down, every
   clock gated, then the chip's own power-down */
bool codec_port_mic_adc_down(i2c_master_bus_handle_t bus, uint8_t addr) {
    if (!bus || !addr || i2c_master_probe(bus, addr, 50) != ESP_OK) return false;
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return false;
    static const uint8_t seq[][2] = {
        { 0x47, 0xFF }, { 0x48, 0xFF }, { 0x49, 0xFF }, { 0x4A, 0xFF }, { 0x4B, 0xFF }, { 0x4C, 0xFF },
        { 0x40, 0xC0 }, { 0x01, 0x7F }, { 0x06, 0x07 },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof seq / sizeof *seq; i++) ok &= i2c_master_transmit(dev, seq[i], 2, 100) == ESP_OK;
    uint8_t reg = 0x06, r06 = 0; i2c_master_transmit_receive(dev, &reg, 1, &r06, 1, 100);
    i2c_master_bus_rm_device(dev);
    ESP_LOGI(TAG, "ES7210 @0x%02x suspended%s (POWER_DOWN %02x)", addr, ok ? "" : " - a write FAILED", r06);
    return ok;
}
