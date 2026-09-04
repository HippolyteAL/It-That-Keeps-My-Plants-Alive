/*
sensors.kicad_sch of the schematic
    - DS18B20 onewire soil probe temperature
    - 3x resistive soil moisture probes (potentiometers in Wokwi)
    - SHT31 ambient temperature/humidity (I2C)
    - BH1750 ambient light (I2C)
    - DS3231 real-time clock (I2C)
*/

#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    float   soil_temp_c;
    float   soil_moisture_pct[3];
    float   ambient_temp_c;
    float   ambient_humidity_pct;
    float   ambient_lux;
    int64_t rtc_unix_time;
    bool    valid;
} sensor_readings_t;

// Bring up I2C bus + devices, ADC unit, and the OneWire GPIO.
esp_err_t sensors_init(void);

// read every sensor and fill out a single struct.
esp_err_t sensors_read_all(sensor_readings_t *out);

// Individual reads, exposed for debugging / use from the control task.
esp_err_t sensors_read_soil_temp(float *out_c);

esp_err_t sensors_read_soil_moisture(uint8_t channel_index, float *out_pct);
esp_err_t sensors_read_ambient(float *out_temp_c, float *out_humidity_pct);
esp_err_t sensors_read_light(float *out_lux);
esp_err_t sensors_read_rtc(int64_t *out_unix_time);

// Useful once to Seeds the RTC from build time, purely a oneof Wokwi simulation command. NOTE: might be unnecessary
esp_err_t sensors_set_rtc(int64_t unix_time);

#endif /* SENSORS_H */