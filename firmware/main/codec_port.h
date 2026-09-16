/* codec_port.h - the ES8311 audio codec the tank never uses (2026-09-13).
 * Its digital side sits on VCC3V3 (the analog side, ALDO1, is switched off
 * at boot). The chip resets into a near power-down state but leaves its
 * internal voltage reference enabled (REG 0x0D bit 2); this puts it fully
 * down over I2C at boot and reads the state back. `codec` on the director
 * prints the registers. */
#ifndef CODEC_PORT_H
#define CODEC_PORT_H
#include <stdbool.h>
#include "driver/i2c_master.h"
bool codec_port_init(i2c_master_bus_handle_t bus);   /* false = no codec answered */
bool codec_port_present(void);
/* the DAC path for the audio port (s3/audio_port_es8311.c): slave I2S, 16-bit,
 * MCLK = 256 fs on the MCLK pin, DAC at 0 dB - the I2S clocks must already
 * run. codec_port_down is the powered-down register set the battery pass
 * measured (SYS0D F8). */
bool codec_port_up(void);
void codec_port_settled(void);
void codec_port_down(void);
void codec_port_dump(void);
#endif
