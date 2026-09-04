#include "sensors.h"
#include "pinout.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"

static const char *TAG = "sensors";

/* I2C + ADC handles, shared across this file's read functions. */
static i2c_master_bus_handle_t   s_i2c_bus     = NULL;
static i2c_master_dev_handle_t   s_dev_sht31   = NULL;
static i2c_master_dev_handle_t   s_dev_bh1750  = NULL;
static i2c_master_dev_handle_t   s_dev_ds3231  = NULL;
static adc_oneshot_unit_handle_t s_adc_handle  = NULL;

/*
SIMULATION-ONLY: Wokwi has no soil moisture sensor part, so J5/J6/J7
are stood in for by potentiometers (Soil_moisture[1/2/3] in diagram.json). 
This maps a raw ADC count to an arbitrary 0-100% moisture reading so the rest 
of the code doesn't need to care about the substitution.

TODO: replace with a real calibration curve once the resistive probes
are the actual sensors.
*/

static float potentiometer_to_moisture_pct(int raw_counts){
    float moisture = raw_counts / 1000;
    if (moisture > 1.0f) {
        return 1.0f;
    }
    return moisture;
}

esp_err_t sensors_init(void) {
    ESP_LOGI(TAG, "sensors_init: stub");
    // TODO: i2c_new_master_bus() on PIN_I2C_SCL / PIN_I2C_SDA -> s_i2c_bus
    // TODO: i2c_master_bus_add_device() for SHT31 / BH1750 / DS3231 using I2C_ADDR_SHT31 / I2C_ADDR_BH1750 / I2C_ADDR_DS3231
    // TODO: adc_oneshot_new_unit(ADC_UNIT_MOISTURE) -> s_adc_handle
    // TODO: adc_oneshot_config_channel() for ADC_CHANNEL_MOISTURE{1,2,3}
    // TODO: configure PIN_ONEWIRE_TEMP as a bit-banged onewire bus for the DS18B20
    return ESP_OK;
}

esp_err_t sensors_read_soil_temp(float *out_c) {
    // TODO: DS18B20 convert-T + read-scratchpad sequence over 1-Wire on PIN_ONEWIRE_TEMP, convert the raw 12-bit reading to degrees C.
    *out_c = 0.0f;
    return ESP_OK;
}

esp_err_t sensors_read_soil_moisture(uint8_t channel_index, float *out_pct) {
    // TODO: adc_oneshot_read() on the channel matching channel_index (0/1/2 -> ADC_CHANNEL_MOISTURE1/2/3), 
    // then run the raw counts through potentiometer_to_moisture_pct() for simulation builds.
    (void)channel_index;
    *out_pct = 0.0f;
    return ESP_OK;
}

esp_err_t sensors_read_ambient(float *out_temp_c, float *out_humidity_pct) {
    // TODO: SHT31 measurement command + 6-byte read (temp, CRC, humidity, CRC) over I2C via s_dev_sht31.
    *out_temp_c = 0.0f;
    *out_humidity_pct = 0.0f;
    return ESP_OK;
}

esp_err_t sensors_read_light(float *out_lux) {
    // TODO: BH1750 one-time H-resolution mode read over I2C via s_dev_bh1750, convert raw counts to lux (raw / 1.2).
    *out_lux = 0.0f;
    return ESP_OK;
}

esp_err_t sensors_read_rtc(int64_t *out_unix_time) {
    // TODO: DS3231 read of seconds to year registers via s_dev_ds3231, BCD -> struct tm -> mktime()/unix time.
    *out_unix_time = 0;
    return ESP_OK;
}

esp_err_t sensors_set_rtc(int64_t unix_time) {
    // TODO: unix time -> struct tm -> BCD, write to DS3231 via s_dev_ds3231. 
    (void)unix_time;
    return ESP_OK;
}

esp_err_t sensors_read_all(sensor_readings_t *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool ok = true;
    esp_err_t err;

    err = sensors_read_soil_temp(&out->soil_temp_c);
    ok &= (err == ESP_OK);

    for (int i = 0; i < 3; i++) {
        err = sensors_read_soil_moisture((uint8_t)i, &out->soil_moisture_pct[i]);
        ok &= (err == ESP_OK);
    }

    err = sensors_read_ambient(&out->ambient_temp_c, &out->ambient_humidity_pct);
    ok &= (err == ESP_OK);

    err = sensors_read_light(&out->ambient_lux);
    ok &= (err == ESP_OK);

    err = sensors_read_rtc(&out->rtc_unix_time);
    ok &= (err == ESP_OK);

    out->valid = ok;
    return ok ? ESP_OK : ESP_FAIL;
}