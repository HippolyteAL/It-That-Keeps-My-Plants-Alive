#ifndef PINOUT_H
#define PINOUT_H

/*
Central pin / bus configuration for the "It That Keeps My Plants Alive" project. Mirrors the net names used on the ESP32_Core sheet of
Schematic.kicad_sch (esp32_core.kicad_sch), which is the hub that the Sensors, Actuators, and Storage sheets all connect back into.

Pin numbers are taken from diagram.json (Wokwi simulation) themselves taken from the KiCad schematic net labels. 
Simulation and real hardware should share the same GPIO map, so this header should not need change between Wokwi and real-hardware builds.
*/

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "hal/adc_types.h"

//I2C bus (shared by SHT31, BH1750, DS3231 - SCL/SDA nets)/
#define PIN_I2C_SCL                 GPIO_NUM_21
#define PIN_I2C_SDA                 GPIO_NUM_22
#define I2C_MASTER_FREQ_HZ          100000
 
#define I2C_ADDR_SHT31              0x44    // TODO: confirm ADDR pin strap on real hardware 
#define I2C_ADDR_BH1750             0x23    // TODO: confirm ADDR pin strap on real hardware 
#define I2C_ADDR_DS3231             0x68
 
// SPI bus (SD card logger - SD_SCK/SD_MOSI/SD_MISO/SD_CS nets) These line up with the ESP32's default VSPI pins.
#define PIN_SD_SCK                  GPIO_NUM_18
#define PIN_SD_MOSI                 GPIO_NUM_23
#define PIN_SD_MISO                 GPIO_NUM_19
#define PIN_SD_CS                   GPIO_NUM_5
#define SD_SPI_HOST                 SPI3_HOST   // VSPI 
 
// DS18B20 - ONEWIRE_TEMP net
#define PIN_ONEWIRE_TEMP             GPIO_NUM_4
 
/*
Soil moisture ADC inputs (MOISTURE{1,2,3}_ADC nets)
In simulation these are potentiometers (Soil_moisture1/2/3 in diagram.json).
On real hardware, they will be resistive soil moisture probes, same GPIO / ADC channel either way.
*/
#define PIN_MOISTURE1_ADC            GPIO_NUM_34    // ADC1_CH6
#define PIN_MOISTURE2_ADC            GPIO_NUM_35    // ADC1_CH7
#define PIN_MOISTURE3_ADC            GPIO_NUM_36    // ADC1_CH0 (SENSOR_VP / VP) 
 
#define ADC_UNIT_MOISTURE            ADC_UNIT_1
#define ADC_CHANNEL_MOISTURE1        ADC_CHANNEL_6
#define ADC_CHANNEL_MOISTURE2        ADC_CHANNEL_7
#define ADC_CHANNEL_MOISTURE3        ADC_CHANNEL_0
 
// Actuators (LED_PWM, PUMP_EN, LIMIT_SW, STEPPER_IN[1..4] nets)
#define PIN_LED_PWM                  GPIO_NUM_25    // LED grow light, gate of Q2 on real HW
#define PIN_PUMP_EN                  GPIO_NUM_17    // Pump enable, gate of Q1 on real HW
#define PIN_LIMIT_SW                 GPIO_NUM_14    // Homing / limit switch, active-low w/ pull-up
 
/* 
Stepper coil pins (28BYJ-48 via ULN2003 driver board).
TODO: confirm the STEPPER_IN[1..4] -> coil terminal mapping on the physical stepper; the names below follow the coil
terminal labels used in diagram.json (A+/A-/B+/B-), not necessarily the board's [IN1..IN4] order. 
*/
#define PIN_STEPPER_COIL_A_PLUS     GPIO_NUM_27     // STEPPER_IN?
#define PIN_STEPPER_COIL_A_MINUS    GPIO_NUM_26     // STEPPER_IN?
#define PIN_STEPPER_COIL_B_PLUS     GPIO_NUM_32     // STEPPER_IN?
#define PIN_STEPPER_COIL_B_MINUS    GPIO_NUM_33     // STEPPER_IN?
 
#endif /* PINOUT_H */