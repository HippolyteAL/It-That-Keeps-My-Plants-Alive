#ifndef STORAGE_H
#define STORAGE_H
 
#include "esp_err.h"
#include "sensors.h"
 
/*
storage.kicad_sch of the schematic: microSD module over SPI
*/

// Mount the SD card and prepare the log file.
esp_err_t storage_init(void);
 
// Append one row of sensor data to the log file.
esp_err_t storage_log_reading(const sensor_readings_t *reading);
 
// Cleanly unmount the SD card (e.g. before deep sleep).
esp_err_t storage_deinit(void);
 
#endif /* STORAGE_H */