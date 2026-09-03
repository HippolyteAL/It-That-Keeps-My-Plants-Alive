/* 
Simulated BH1750 ambient light sensor for Wokwi. Implements the real BH1750 I2C protocol 
so an ESP-IDF BH1750 driver code works unmodified on this simulated chip.

The BH1750 has no register-pointer model, so every byte written to it is a command 
(power on/off, reset, select a measurement mode), and every read simply returns the 
current 2-byte light-level value, MSB first.

Measurement-mode differences (H-res / H-res mode 2 / L-res) and the real chip's ~120ms 
conversion delay are not modeled. Read return the current lux value immediately regardless 
of which mode command was senmt last.
*/

#include "wokwi-api.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define BH1750_I2C_ADDRESS 0x23  // TODO: confirm wether the address is this (low) or 0x5C (high) 

typedef struct {
    pin_t scl;
    pin_t sda;
    i2c_dev_t i2c;
    uint32_t attr_lux;
    uint8_t mode;        // last command byte written; 0x00 = power down
    uint8_t byte_index;  // which of the 2 data bytes the next read returns
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
    chip_state_t *chip = (chip_state_t *)user_data;
    chip->byte_index = 0;
    return true;
}

// Returns a "count" that converts to lux at a 1 to 1.2 ratio
static uint8_t on_i2c_read(void *user_data) {
    chip_state_t *chip = (chip_state_t *)user_data;
    
    uint16_t raw = 0;
    if (chip->mode != 0x00) {  // 0x00 = power-down
        uint32_t lux = attr_read(chip->attr_lux);
        uint32_t scaled = lux * 12 / 10;  // BH1750 H-res formula: raw = lux * 1.2
        raw = (scaled > 0xFFFF) ? 0xFFFF : (uint16_t)scaled;
    }
    
    uint8_t value = (chip->byte_index == 0) ? (uint8_t)((raw >> 8) & 0xFF) : (uint8_t)(raw & 0xFF);
    chip->byte_index = (chip->byte_index + 1) % 2;
    return value;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
    chip_state_t *chip = (chip_state_t *)user_data;
    chip->mode = data;
    return true;
}

void chip_init(void) {
    chip_state_t *chip = calloc(1, sizeof(chip_state_t));
    
    chip->scl = pin_init("SCL", INPUT_PULLUP);
    chip->sda = pin_init("SDA", INPUT_PULLUP);
    chip->attr_lux = attr_init("lux", 300);  // 300 is a reasonable ambient indoor light
    
    const i2c_config_t i2c_config = {
        .user_data = chip,
        .address = BH1750_I2C_ADDRESS,
        .scl = chip->scl,
        .sda = chip->sda,
        .connect = on_i2c_connect,
        .read = on_i2c_read,
        .write = on_i2c_write,
    };
    chip->i2c = i2c_init(&i2c_config);
    
    printf("BH1750 simulator ready -- starting at %d lx\n", (int)attr_read(chip->attr_lux));
}