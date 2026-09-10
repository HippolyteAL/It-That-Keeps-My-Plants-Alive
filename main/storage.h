#ifndef STORAGE_H
#define STORAGE_H
 
#include <stdint.h>
#include "esp_err.h"
#include "sensors.h"
 
/*
storage.kicad_sch of the schematic: microSD module over SPI.
*/

// Mount the SD card and prepare the log file.
esp_err_t storage_init(void);

/* Called once per sensor poll. Checks reading->rtc_unix_time against each log's own interval and only
writes a row when enough time has elapsed. Readings where reading->valid is false are skipped entirely 
rather than writing a partial/garbage row into the history. */
esp_err_t storage_log_reading(const sensor_readings_t *reading);
 
/* One row per watering event. volume_ml isn't measurable yet (the pump is just an LED in the wokwi sim). Passes -1.0f for it
until real hardware can report a real volume. Mostly a placeholder for now. TODO implement real water volume rading for hardware. */
esp_err_t storage_log_water_event(int64_t unix_time, uint8_t zone_index, float volume_ml);
 
/* One line per failed sensor read or actuator command. Timestamped by uptime (esp_timer_get_time()), not the RTC since an RTC
failure itself is one of the things this needs to be able to log. "source" should be a short, stable string identifying what failed, 
"err" is whatever esp_err_t is returned. Plain text for now; may move to a JSON structure later to lookup table of error codes for format. */
esp_err_t storage_log_error(const char *source, esp_err_t err);
 
// Cleanly unmount the SD card (e.g. before deep sleep).
esp_err_t storage_deinit(void);
 
#endif /* STORAGE_H */