#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"

#include "pinout.h"
#include "sensors.h"
#include "actuators.h"
#include "storage.h"

/*
esp32_core.kicad_sch. Defines and executes the shared system state, task scheduling, 
and the plant-care control loop.
*/

static const char *TAG = "main";

//TODO placeholder values, revisit once sensors are calibrated
#define SENSOR_POLL_PERIOD_MS       (5 * 1000)
#define CONTROL_LOOP_PERIOD_MS      (10 * 1000)
#define LOGGING_PERIOD_MS           (15 * 1000) // TODO: Should be every 15 minutes (15 x 60000) outside of testing ?

#define MOISTURE_LOW_THRESHOLD_PCT  30.0f
#define TARGET_AMBIENT_LUX          8000.0f

// Shared system state: written by sensor_task, read by control_task and logging_task. 
// Guarded by a mutex since it's touched from three tasks.
typedef struct {
    sensor_readings_t   latest;
    SemaphoreHandle_t   mutex;
} system_state_t;

static system_state_t s_state;

static void sensor_task(void *arg);
static void control_task(void *arg);
static void logging_task(void *arg);

void app_main(void) {
    ESP_LOGI(TAG, "boot: It That Keeps My Plants Alive");

    s_state.mutex = xSemaphoreCreateMutex();
    configASSERT(s_state.mutex != NULL);

    ESP_ERROR_CHECK(sensors_init());
    ESP_ERROR_CHECK(actuators_init());
    ESP_ERROR_CHECK(storage_init());
    // ESP_ERROR_CHECK(actuators_stepper_home());

    xTaskCreate(sensor_task,  "sensor_task",  4096, NULL, 5, NULL);
    xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);
    xTaskCreate(logging_task, "logging_task", 4096, NULL, 4, NULL);
}

static void sensor_task(void *arg) {
    for (;;) {
        sensor_readings_t reading;
        if (sensors_read_all(&reading) == ESP_OK) {
            xSemaphoreTake(s_state.mutex, portMAX_DELAY);
            s_state.latest = reading;
            xSemaphoreGive(s_state.mutex);

            // testing wokwi setup
            ESP_LOGI(TAG, "sensor_task: rtc=%" PRId64 " soil_temp=%.2fC moisture=[%.1f, %.1f, %.1f]%% " "ambient=%.2fC/%.1f%%RH lux=%.1f",
                    reading.rtc_unix_time, reading.soil_temp_c, reading.soil_moisture_pct[0], reading.soil_moisture_pct[1], 
                    reading.soil_moisture_pct[2], reading.ambient_temp_c, reading.ambient_humidity_pct, reading.ambient_lux);
        } else {
            ESP_LOGW(TAG, "sensor_task: read_all failed");
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_PERIOD_MS));
    }
}

static void control_task(void *arg) {
    for (;;) {
        sensor_readings_t reading;
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
        reading = s_state.latest;
        xSemaphoreGive(s_state.mutex);

        if (reading.valid) {
            /* TODO: this reacts the instant a probe dips below threshold. The final goal is to require it to stay below threshold for a
            minimum duration first, so a single noisy/borderline reading doesn't trigger a watering cycle, and so watering cycles can be
            potentially executed all at once. This might require moving the homing call outside the watering call to here. */
            for (uint8_t zone = 0; zone < 3; zone++) {
                if (reading.soil_moisture_pct[zone] < MOISTURE_LOW_THRESHOLD_PCT) {
                    ESP_LOGI(TAG, "control_task: zone %u dry (%.1f%%), watering", zone, reading.soil_moisture_pct[zone]);
                    esp_err_t err = actuators_water_zone(zone, reading.rtc_unix_time);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "control_task: water_zone(%u) failed: %s", zone, esp_err_to_name(err));
                    }
                }
            }
            
            uint8_t led_percent = 0;
            actuators_set_led_for_target_lux(reading.ambient_lux, TARGET_AMBIENT_LUX, &led_percent);
            ESP_LOGI(TAG, "control_task: ambient=%.1flux led=%u%% (target=%.0flux)", reading.ambient_lux, led_percent, TARGET_AMBIENT_LUX);
        }

        if (actuators_limit_switch_triggered()) {
            // TODO: decide what a manual homing-button press should do mid-run.
            ESP_LOGI(TAG, "control_task: homing button pressed");
        }

        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS));
    }
}

static void logging_task(void *arg) {
    for (;;) {
        sensor_readings_t reading;
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
        reading = s_state.latest;
        xSemaphoreGive(s_state.mutex);

        if (reading.valid) {
            esp_err_t err = storage_log_reading(&reading);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "logging_task: write failed (%s)", esp_err_to_name(err));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LOGGING_PERIOD_MS));
    }
}