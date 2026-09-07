#include "sensors.h"
#include "pinout.h"
 
#include <stddef.h>
 
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

// Onewire setup ----------------------------------------------------------------------------------

#define ONEWIRE_CMD_SKIP_ROM         0xCC
#define DS18B20_CMD_CONVERT_T        0x44
#define DS18B20_CMD_READ_SCRATCHPAD  0xBE
static portMUX_TYPE s_onewire_mux = portMUX_INITIALIZER_UNLOCKED;

static void onewire_bus_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << PIN_ONEWIRE_TEMP,
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,  // open-drain
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_ONEWIRE_TEMP, 1);    // idle high
}

static bool onewire_reset_pulse(void) {
    gpio_set_level(PIN_ONEWIRE_TEMP, 0);    // resets pulse and hold low
    esp_rom_delay_us(480);
    gpio_set_level(PIN_ONEWIRE_TEMP, 1);   // releases and wait for a presence pulse (pulling low)
    esp_rom_delay_us(70);
    bool presence = (gpio_get_level(PIN_ONEWIRE_TEMP) == 0);
    esp_rom_delay_us(410);
    return presence;    // returns true if a device pulled the bus low in response
}

static void onewire_write_bit(bool bit) {
    taskENTER_CRITICAL(&s_onewire_mux);
    if (bit) {
        gpio_set_level(PIN_ONEWIRE_TEMP, 0);
        esp_rom_delay_us(6);
        gpio_set_level(PIN_ONEWIRE_TEMP, 1);
        esp_rom_delay_us(64);
    } else {
        gpio_set_level(PIN_ONEWIRE_TEMP, 0);
        esp_rom_delay_us(60);
        gpio_set_level(PIN_ONEWIRE_TEMP, 1);
        esp_rom_delay_us(10);
    }
    taskEXIT_CRITICAL(&s_onewire_mux);
}
static bool onewire_read_bit(void) {
    bool bit;
    taskENTER_CRITICAL(&s_onewire_mux);
    gpio_set_level(PIN_ONEWIRE_TEMP, 0);
    esp_rom_delay_us(6);
    gpio_set_level(PIN_ONEWIRE_TEMP, 1);
    esp_rom_delay_us(9);
    bit = (gpio_get_level(PIN_ONEWIRE_TEMP) != 0);
    esp_rom_delay_us(55);
    taskEXIT_CRITICAL(&s_onewire_mux);
    return bit;
}
static void onewire_write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(byte & 0x01);
        byte >>= 1;
    }
}
static uint8_t onewire_read_byte(void) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte >>= 1;
        if (onewire_read_bit()) {
            byte |= 0x80;
        }
    }
    return byte;
}

// Dallas/Maxim CRC8 (poly x^8+x^5+x^4+1, reflected 0x8C), used to verify the DS18B20 scratchpad.
static uint8_t onewire_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t in_byte = data[i];
        for (int b = 0; b < 8; b++) {
            uint8_t mix = (crc ^ in_byte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            in_byte >>= 1;
        }
    }
    return crc;
}

// Initialization ---------------------------------------------------------------------------------

esp_err_t sensors_init(void) {
    ESP_LOGI(TAG, "sensors_init");
    // TODO: i2c_new_master_bus() on PIN_I2C_SCL / PIN_I2C_SDA -> s_i2c_bus
    // TODO: i2c_master_bus_add_device() for SHT31 / BH1750 / DS3231 using I2C_ADDR_SHT31 / I2C_ADDR_BH1750 / I2C_ADDR_DS3231
    // TODO: adc_oneshot_new_unit(ADC_UNIT_MOISTURE) -> s_adc_handle
    // TODO: adc_oneshot_config_channel() for ADC_CHANNEL_MOISTURE{1,2,3}

    onewire_bus_init();

    return ESP_OK;
}

// Exposed functions ------------------------------------------------------------------------------

esp_err_t sensors_read_soil_temp(float *out_c) {
    if (out_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
 
    if (!onewire_reset_pulse()) {
        ESP_LOGW(TAG, "soil_temp: no OneWire presence pulse on GPIO%d - check wiring", PIN_ONEWIRE_TEMP);
        return ESP_ERR_NOT_FOUND;
    }
 
    onewire_write_byte(ONEWIRE_CMD_SKIP_ROM);      // single device on the bus
    onewire_write_byte(DS18B20_CMD_CONVERT_T);
 
    /* Default 12-bit resolution conversion takes up to 750ms. This blocks
     * whichever task calling this (sensor_task) for that long, which is fine
     * because of the 5s poll period. */
    vTaskDelay(pdMS_TO_TICKS(750));
 
    if (!onewire_reset_pulse()) {
        ESP_LOGW(TAG, "soil_temp: lost presence before scratchpad read");
        return ESP_ERR_NOT_FOUND;
    }
 
    onewire_write_byte(ONEWIRE_CMD_SKIP_ROM);
    onewire_write_byte(DS18B20_CMD_READ_SCRATCHPAD);
 
    uint8_t scratchpad[9];
    for (int i = 0; i < 9; i++) {
        scratchpad[i] = onewire_read_byte();
    }
 
    if (onewire_crc8(scratchpad, 8) != scratchpad[8]) {
        ESP_LOGW(TAG, "soil_temp: scratchpad CRC mismatch");    // electric problem (missing pull-up, bad ground)
        return ESP_ERR_INVALID_CRC;
    }
 
    // scratchpad bytes 0-1 are the temperature register, LSB first, 1/16 degC per LSB at the default 12-bit resolution
    int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
    *out_c = raw / 16.0f;
 
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