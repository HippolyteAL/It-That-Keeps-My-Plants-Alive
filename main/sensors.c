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

// I2C + ADC handles*/
static i2c_master_bus_handle_t   s_i2c_bus     = NULL;
static i2c_master_dev_handle_t   s_dev_sht31   = NULL;
static i2c_master_dev_handle_t   s_dev_bh1750  = NULL;
static i2c_master_dev_handle_t   s_dev_ds3231  = NULL;
static adc_oneshot_unit_handle_t s_adc_handle  = NULL;
static const adc_channel_t s_moisture_channels[3] = {
    ADC_CHANNEL_MOISTURE1, ADC_CHANNEL_MOISTURE2, ADC_CHANNEL_MOISTURE3
};

/*
SIMULATION-ONLY: Wokwi has no soil moisture sensor part, so J5/J6/J7
are stood in for by potentiometers (Soil_moisture[1/2/3] in diagram.json). 
This maps a raw ADC count to an arbitrary 0-100% moisture reading so the rest 
of the code doesn't need to care about the substitution.

TODO: replace with a real calibration curve once the resistive probes
are the actual sensors.
*/

#define POT_ADC_MAX_RAW 4095.0f // ADC_BITWIDTH_DEFAULT is 12-bit on ESP32 (2^12 = 4096)

static float potentiometer_to_moisture_pct(int raw_counts){
    float pct = ((float)raw_counts / POT_ADC_MAX_RAW) * 100.0f;
    if (pct > 100.0f) {
        pct = 100.0f;
    } else if (pct < 0.0f) {
        pct = 0.0f;
    }
    return pct;
}

// Onewire helpers --------------------------------------------------------------------------------

#define ONEWIRE_CMD_SKIP_ROM         0xCC
#define DS18B20_CMD_CONVERT_T        0x44
#define DS18B20_CMD_READ_SCRATCHPAD  0xBE
static portMUX_TYPE s_onewire_mux = portMUX_INITIALIZER_UNLOCKED;

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
static uint8_t onewire_crc8(const uint8_t *data, size_t len) {
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

// Sensirion CRC8 (poly x^8+x^5+x^4+1 = 0x31, non-reflected, init 0xFF), used to verify each 16-bit word from SHT31
static uint8_t sensirion_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// DS3231 helpers ---------------------------------------------------------------------------------

// NOTE:  The DS3231 is treated as UTC since it is the most common from what i found, but this could have to be changed
// algorithms from: https://howardhinnant.github.io/date_algorithms.html

static inline uint8_t bcd_to_dec(uint8_t bcd) {
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}
static inline uint8_t dec_to_bcd(uint8_t dec) {
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

static int64_t civil_to_unix(int year, int month, int day, int hour, int min, int sec) {
    int64_t y = year;
    y -= (month <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + (unsigned)day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;    // days since 1970-01-01
    return days * 86400 + hour * 3600 + min * 60 + sec;
}
static void unix_to_civil(int64_t unix_time, int *year, int *month, int *day, int *hour, int *min, int *sec) {
    int64_t days = unix_time / 86400;
    int64_t rem = unix_time % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    *hour = (int)(rem / 3600);
    *min  = (int)((rem % 3600) / 60);
    *sec  = (int)(rem % 60);
    
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    int m = (int)mp + (mp < 10 ? 3 : -9);
    int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    
    *year  = (int)(y + (m <= 2));
    *month = m;
    *day   = d;
}

// Initialization ---------------------------------------------------------------------------------

// I2C bus (SHT31 / BH1750 / DS3231)
static esp_err_t i2c_bus_init(void) {
    esp_err_t err;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }
 
    i2c_device_config_t sht31_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_SHT31,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &sht31_cfg, &s_dev_sht31);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add SHT31 device failed: %s", esp_err_to_name(err));
        return err;
    }
 
    i2c_device_config_t bh1750_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_BH1750,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &bh1750_cfg, &s_dev_bh1750);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add BH1750 device failed: %s", esp_err_to_name(err));
        return err;
    }
 
    i2c_device_config_t ds3231_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_DS3231,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &ds3231_cfg, &s_dev_ds3231);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add DS3231 device failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

//BH1750 luminosity sensor, turn on and set to high resolution mode, each read is 2-byte
static esp_err_t luminosity_sensor_init(void) {
    esp_err_t err;

    uint8_t bh1750_power_on = 0x01;
    err = i2c_master_transmit(s_dev_bh1750, &bh1750_power_on, 1, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 power-on failed: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t bh1750_cont_hres = 0x10;
    err = i2c_master_transmit(s_dev_bh1750, &bh1750_cont_hres, 1, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BH1750 mode-select failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(180));     // lets the first conversion land before anyone reads

    return ESP_OK;
}

// Capacitive soil moisture sensors x3
static esp_err_t ADC_init(void) {
    esp_err_t err;

    adc_oneshot_unit_init_cfg_t adc_unit_cfg = {
        .unit_id = ADC_UNIT_MOISTURE,
    };
    err = adc_oneshot_new_unit(&adc_unit_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }
 
    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,   /* full 0-3.3V input range */
    };
    for (int i = 0; i < 3; i++) {
        err = adc_oneshot_config_channel(s_adc_handle, s_moisture_channels[i], &adc_chan_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adc_oneshot_config_channel[%d] failed: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

// DS18B20 soil temperature
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

esp_err_t sensors_init(void) {
    ESP_LOGI(TAG, "sensors_init");
    esp_err_t err;

    err = i2c_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failure in sensors_init");
        return err;
    }

    err = luminosity_sensor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failure in sensors_init");
        return err;
    }
    
    err = ADC_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failure in sensors_init");
        return err;
    }

    onewire_bus_init();

    return ESP_OK;
}

// Non-initialization exposed functions -----------------------------------------------------------

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

    // debugging, returning all FF after first read, seems to be a simulated chip bug, backlogged / might not fix just for the simulation
    ESP_LOGI(TAG, "soil_temp: scratchpad = %02X %02X %02X %02X %02X %02X %02X %02X %02X, computed_crc = %02X",
             scratchpad[0], scratchpad[1], scratchpad[2], scratchpad[3], scratchpad[4],
             scratchpad[5], scratchpad[6], scratchpad[7], scratchpad[8],
             onewire_crc8(scratchpad, 8));
 
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
    if (out_pct == NULL || channel_index > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, s_moisture_channels[channel_index], &raw);
    if (err != ESP_OK) {
        return err;
    }
    
    *out_pct = potentiometer_to_moisture_pct(raw);
    return ESP_OK;
}

esp_err_t sensors_read_ambient(float *out_temp_c, float *out_humidity_pct) {
    if (out_temp_c == NULL || out_humidity_pct == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t cmd[2] = {0x24, 0x00};
    esp_err_t err = i2c_master_transmit(s_dev_sht31, cmd, sizeof(cmd), -1);
    if (err != ESP_OK) {
        return err;
    }
    
    vTaskDelay(pdMS_TO_TICKS(20));  // max ~15ms conversion time
    
    uint8_t data[6];
    err = i2c_master_receive(s_dev_sht31, data, sizeof(data), -1);
    if (err != ESP_OK) {
        return err;
    }
    
    if (sensirion_crc8(&data[0], 2) != data[2] || sensirion_crc8(&data[3], 2) != data[5]) {
        ESP_LOGW(TAG, "ambient: SHT31 CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }
    
    uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_hum  = ((uint16_t)data[3] << 8) | data[4];
    
    *out_temp_c        = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *out_humidity_pct  = 100.0f * ((float)raw_hum / 65535.0f);
    return ESP_OK;
}

esp_err_t sensors_read_light(float *out_lux) {
    if (out_lux == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
 
    uint8_t data[2];
    esp_err_t err = i2c_master_receive(s_dev_bh1750, data, sizeof(data), -1);
    if (err != ESP_OK) {
        return err;
    }
 
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    *out_lux = (float)raw / 1.2f;   // conversion from the datasheet for Hi-res mode
    return ESP_OK;
}

esp_err_t sensors_read_rtc(int64_t *out_unix_time) {
    if (out_unix_time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
 
    uint8_t reg = 0x00; // seconds register
    uint8_t data[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev_ds3231, &reg, 1, data, sizeof(data), -1);
    if (err != ESP_OK) {
        return err;
    }
 
    int sec   = bcd_to_dec(data[0] & 0x7F);
    int min   = bcd_to_dec(data[1] & 0x7F);
    int hour  = bcd_to_dec(data[2] & 0x3F); // assumes 24-hour mode
    int day   = bcd_to_dec(data[4] & 0x3F);
    int month = bcd_to_dec(data[5] & 0x1F);
    int year  = 2000 + bcd_to_dec(data[6]);
    // ignores the century bit
    
    *out_unix_time = civil_to_unix(year, month, day, hour, min, sec);
    return ESP_OK;
}

// Possible only in the wokwi simulator, the deal DS3231 chip doesn't allow writes, used for debugging / testing
esp_err_t sensors_set_rtc(int64_t unix_time) {
    int year, month, day, hour, min, sec;
    unix_to_civil(unix_time, &year, &month, &day, &hour, &min, &sec);
 
    if (year < 2000 || year > 2099) {
        return ESP_ERR_INVALID_ARG;     // 2-digit year register
    }
 
    uint8_t data[8];
    data[0] = 0x00;
    data[1] = dec_to_bcd((uint8_t)sec);
    data[2] = dec_to_bcd((uint8_t)min);
    data[3] = dec_to_bcd((uint8_t)hour);    // 24-hour mode
    data[4] = dec_to_bcd(1);                // day-of-week unused
    data[5] = dec_to_bcd((uint8_t)day);
    data[6] = dec_to_bcd((uint8_t)month);
    data[7] = dec_to_bcd((uint8_t)(year - 2000));
 
    return i2c_master_transmit(s_dev_ds3231, data, sizeof(data), -1);
}

esp_err_t sensors_read_all(sensor_readings_t *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool ok = true;
    esp_err_t err;

    // Backlogged for now, faulty simulated chip and not necessary for the project as a whole
    // err = sensors_read_soil_temp(&out->soil_temp_c);
    // ok &= (err == ESP_OK);

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