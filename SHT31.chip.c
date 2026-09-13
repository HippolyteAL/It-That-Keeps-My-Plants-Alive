/*
Author: Hippolyte Audet-Lagacé

Simulated SHT31 temperature/humidity sensor for Wokwi. Implements the real SHT31 I2C protocol 
including the datasheet's actual CRC-8 checksum generation, so an existing ESP-IDF SHT31 driver code
works unmodified on the simulated chip.

Any command whose first byte starts with 0x2 (the entire single-shot and periodic measurement 
command family) is treated as  "start a measurement". 
Repeatability/mode differences and the real ~15ms conversion delay are not modeled.

The status register (0xF32D) and heater commands are acknowledged but not implemented (returns 0).

Soft reset (0x30A2) is accepted but does nothing since this simulation has no persistent fault state.
*/

#include "wokwi-api.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define SHT31_I2C_ADDRESS 0x44  // TODO: confirm wether the address is this (low) or 0x45 (high)

typedef struct {
    pin_t scl;
    pin_t sda;
    i2c_dev_t i2c;
    uint32_t attr_temperature;
    uint32_t attr_humidity;
    uint8_t cmd_byte_count;  // Which byte of the 2-byte command is next
    uint8_t cmd_msb;
    uint8_t response[6];     // pre-built [tempMSB,tempLSB,tempCRC,humMSB,humLSB,humCRC]
    uint8_t read_index;      // which byte of response[] the next read returns
} chip_state_t;

// Checksum: datasheet section 4.12
static uint8_t crc8(uint8_t msb, uint8_t lsb) {
    uint8_t data[2] = {msb, lsb};
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static void build_response(chip_state_t *chip) {
    int temp_c = (int)attr_read(chip->attr_temperature);
    int hum_rh = (int)attr_read(chip->attr_humidity);
    
    // Datasheet conversion formulas, inverted to go from real-world value to raw counts
    uint32_t raw_temp = (uint32_t)(((temp_c + 45) * 65535L) / 175);
    uint32_t raw_hum  = (uint32_t)((hum_rh * 65535L) / 100);
    if (raw_temp > 0xFFFF) raw_temp = 0xFFFF;
    if (raw_hum  > 0xFFFF) raw_hum  = 0xFFFF;
    
    uint8_t t_msb = (uint8_t)((raw_temp >> 8) & 0xFF);
    uint8_t t_lsb = (uint8_t)(raw_temp & 0xFF);
    uint8_t h_msb = (uint8_t)((raw_hum >> 8) & 0xFF);
    uint8_t h_lsb = (uint8_t)(raw_hum & 0xFF);
    
    chip->response[0] = t_msb;
    chip->response[1] = t_lsb;
    chip->response[2] = crc8(t_msb, t_lsb);
    chip->response[3] = h_msb;
    chip->response[4] = h_lsb;
    chip->response[5] = crc8(h_msb, h_lsb);
    chip->read_index = 0;
}
 
static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
    chip_state_t *chip = (chip_state_t *)user_data;
    if (read) {
        // read command -> build the 6-byte response from the sliders current values.
        build_response(chip);
    } else {
        // write command -> a fresh 2-byte command follows.
        chip->cmd_byte_count = 0;
    }
    return true;
}

static uint8_t on_i2c_read(void *user_data) {
    chip_state_t *chip = (chip_state_t *)user_data;
    uint8_t value = chip->response[chip->read_index];
    if (chip->read_index < 5) chip->read_index++;   // hold at last byte if over-read
    return value;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
    chip_state_t *chip = (chip_state_t *)user_data;
    
    if (chip->cmd_byte_count == 0) {
        chip->cmd_msb = data;
        chip->cmd_byte_count = 1;
    } else {
        chip->cmd_byte_count = 0;
    }
    return true;
}

void chip_init(void) {
    chip_state_t *chip = calloc(1, sizeof(chip_state_t));
    
    chip->scl = pin_init("SCL", INPUT_PULLUP);
    chip->sda = pin_init("SDA", INPUT_PULLUP);
    
    chip->attr_temperature = attr_init("temperature", 21.5);    // room temperature default
    chip->attr_humidity    = attr_init("humidity", 40);         // typical indoor RH default
    
    const i2c_config_t i2c_config = {
        .user_data = chip,
        .address = SHT31_I2C_ADDRESS,
        .scl = chip->scl,
        .sda = chip->sda,
        .connect = on_i2c_connect,
        .read = on_i2c_read,
        .write = on_i2c_write,
    };
    chip->i2c = i2c_init(&i2c_config);
    
    printf("SHT31 simulator ready -- starting at %d C, %d%%RH\n",
        (int)attr_read(chip->attr_temperature), (int)attr_read(chip->attr_humidity));
}