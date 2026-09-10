#include "storage.h"
#include "pinout.h"

#include <stdio.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "storage";

#define MOUNT_POINT   "/sdcard"

#define AMBIENT_LOG_PATH   MOUNT_POINT "/AMBIENT.CSV"
#define SOIL_LUX_LOG_PATH  MOUNT_POINT "/SOIL_LUX.CSV"
#define WATER_LOG_PATH     MOUNT_POINT "/WATER.CSV"
#define ERROR_LOG_PATH     MOUNT_POINT "/ERRORS.TXT"

// Easily editable for testing
#define AMBIENT_LOG_INTERVAL_SEC    (60 * 1)   // ambient temp and humidity: hourly 
#define SOIL_LUX_LOG_INTERVAL_SEC   (15 * 1)   // soil moisture and lux: every 15 min 

static sdmmc_card_t *s_card = NULL;
static SemaphoreHandle_t s_storage_mutex = NULL;

// Last RTC time (unix seconds) each periodic log actually wrote a row, with a log at 0.
static int64_t s_last_ambient_log_unix  = 0;
static int64_t s_last_soil_lux_log_unix = 0;

// Helpers ----------------------------------------------------------------------------------------

static esp_err_t ensure_csv_header(const char *path, const char *header_line){
    FILE *f = fopen(path, "r");
    if (f != NULL) {
        fclose(f);
        return ESP_OK;  // already exists
    }
 
    f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "failed to create %s", path);
        return ESP_FAIL;
    }
    fputs(header_line, f);
    fclose(f);
    return ESP_OK;
}

// Initialization ---------------------------------------------------------------------------------

esp_err_t storage_init(void) {
    esp_err_t err;

    s_storage_mutex = xSemaphoreCreateMutex();
    if (s_storage_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_SD_MOSI,
        .miso_io_num     = PIN_SD_MISO,
        .sclk_io_num     = PIN_SD_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };
    err = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_SD_CS;
    slot_cfg.host_id = SD_SPI_HOST;
    
    /* TODO/IMPORTANT: the simulated Wokwi card starts out blank/unformatted, format_if_mount_failed = true lets the 
    first mount succeed by formatting it. Flip to false before pointing this at a real card that might hold other data,
    it can and will get wiped */
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files              = 6,
        .allocation_unit_size   = 16 * 1024,
    };
    
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_POINT);
 
    err = ensure_csv_header(AMBIENT_LOG_PATH, "unix_time,ambient_temp_c,ambient_humidity_pct,soil_temp_c\n");
    if (err != ESP_OK) {
        return err;
    }
    err = ensure_csv_header(SOIL_LUX_LOG_PATH, "unix_time,moisture1_pct,moisture2_pct,moisture3_pct,ambient_lux\n");
    if (err != ESP_OK) {
        return err;
    }
    // volume_ml is -1 whenever it isn't measurable yet (see storage.h). 
    err = ensure_csv_header(WATER_LOG_PATH, "unix_time,zone_index,volume_ml\n");
    if (err != ESP_OK) {
        return err;
    }

    //TODO?: add printf functions to read the logged data, since the vscode extension sometimes doesnt allow inspecting the virtual MicroSD.

    return ESP_OK;
}

// Logging operations -----------------------------------------------------------------------------

esp_err_t storage_log_reading(const sensor_readings_t *reading) {
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!reading->valid) {
        // skip fully/partially failed samples. TODO: handle this better?
        return ESP_OK;
    }
 
    esp_err_t err = ESP_OK;
 
    xSemaphoreTake(s_storage_mutex, portMAX_DELAY);
 
    if (reading->rtc_unix_time - s_last_ambient_log_unix >= AMBIENT_LOG_INTERVAL_SEC) {
        FILE *f = fopen(AMBIENT_LOG_PATH, "a");
        if (f != NULL) {
            fprintf(f, "%" PRId64 ",%.2f,%.2f,%.2f\n", 
                reading->rtc_unix_time, 
                reading->ambient_temp_c, 
                reading->ambient_humidity_pct, 
                reading->soil_temp_c);
            fclose(f);
            s_last_ambient_log_unix = reading->rtc_unix_time;
            ESP_LOGI(TAG, "storage: logged hourly data");
        } else {
            ESP_LOGE(TAG, "failed to open %s for append", AMBIENT_LOG_PATH);
            err = ESP_FAIL;
        }
    }
 
    if (reading->rtc_unix_time - s_last_soil_lux_log_unix >= SOIL_LUX_LOG_INTERVAL_SEC) {
        FILE *f = fopen(SOIL_LUX_LOG_PATH, "a");
        if (f != NULL) {
            fprintf(f, "%" PRId64 ",%.2f,%.2f,%.2f,%.2f\n",
                    reading->rtc_unix_time,
                    reading->soil_moisture_pct[0], 
                    reading->soil_moisture_pct[1],
                    reading->soil_moisture_pct[2], 
                    reading->ambient_lux);
            fclose(f);
            s_last_soil_lux_log_unix = reading->rtc_unix_time;
            ESP_LOGI(TAG, "storage: logged 0.25-hourly data");
        } else {
            ESP_LOGE(TAG, "failed to open %s for append", SOIL_LUX_LOG_PATH);
            err = ESP_FAIL;
        }
    }
 
    xSemaphoreGive(s_storage_mutex);
 
    return err;
}

esp_err_t storage_log_water_event(int64_t unix_time, uint8_t zone_index, float volume_ml) {
    esp_err_t err = ESP_OK;
 
    xSemaphoreTake(s_storage_mutex, portMAX_DELAY);
 
    FILE *f = fopen(WATER_LOG_PATH, "a");
    if (f != NULL) {
        fprintf(f, "%" PRId64 ",%u,%.2f\n", unix_time, (unsigned)zone_index, volume_ml);
        fclose(f);
    } else {
        ESP_LOGE(TAG, "failed to open %s for append", WATER_LOG_PATH);
        err = ESP_FAIL;
    }
 
    xSemaphoreGive(s_storage_mutex);
 
    return err;
}

esp_err_t storage_log_error(const char *source, esp_err_t error)
{
    if (source == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
 
    esp_err_t err = ESP_OK;
 
    xSemaphoreTake(s_storage_mutex, portMAX_DELAY);
 
    FILE *f = fopen(ERROR_LOG_PATH, "a");
    if (f != NULL) {
        int64_t uptime_sec = esp_timer_get_time() / 1000000;
        fprintf(f, "[%" PRId64 "s] %s: %s (0x%x)\n", uptime_sec, source, esp_err_to_name(error), error);
        fclose(f);
    } else {
        ESP_LOGE(TAG, "failed to open %s for append", ERROR_LOG_PATH);
        err = ESP_FAIL;
    }
 
    xSemaphoreGive(s_storage_mutex);
 
    return err;
}
 
esp_err_t storage_deinit(void) {
    esp_err_t err = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
 
    if (s_storage_mutex != NULL) {
        vSemaphoreDelete(s_storage_mutex);
        s_storage_mutex = NULL;
    }
 
    return err;
}