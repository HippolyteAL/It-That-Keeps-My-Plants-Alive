#include "storage.h"
#include "pinout.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";

#define MOUNT_POINT   "/sdcard"
#define LOG_FILENAME  MOUNT_POINT "/plantlog.csv"

static sdmmc_card_t *s_card = NULL;

esp_err_t storage_init(void) {
    ESP_LOGI(TAG, "storage_init: stub");
    // TODO: spi_bus_initialize() on SD_SPI_HOST with PIN_SD_MOSI/MISO/SCK
    // TODO: sdspi_device_config_t on PIN_SD_CS
    // TODO: esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card)
    // TODO: if LOG_FILENAME doesn't exist yet, create it and write a CSV header row (example: timestamp, soil_temp_c,
    //       moisture1_pct, moisture2_pct, moisture3_pct, ambient_temp_c, ambient_humidity_pct, ambient_lux) 
    return ESP_OK;
}

esp_err_t storage_log_reading(const sensor_readings_t *reading) {
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // TODO: fopen(LOG_FILENAME, "a"), fprintf one CSV row from "reading", fclose(). 
    // Keep this call fast/non-blocking (ideally) since it runs from logging_task on a timer, not from an ISR.
    // NOTE: this might not be a good way to do this, but if it works, great!
    return ESP_OK;
}
 
esp_err_t storage_deinit(void)
{
    // TODO: esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card)
    return ESP_OK;
}