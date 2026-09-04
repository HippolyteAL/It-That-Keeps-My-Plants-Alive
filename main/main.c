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
main.c

esp32_core.kicad_sch. Defines and executes the shared system state, task scheduling, 
and the plant-care control loop.
*/

static const char *TAG = "main";

//TODO placeholder values, revisit once sensors are calibrated
#define SENSOR_POLL_PERIOD_MS       (5 * 1000)
#define CONTROL_LOOP_PERIOD_MS      (10 * 1000)
#define LOGGING_PERIOD_MS           (60 * 1000)

#define MOISTURE_LOW_THRESHOLD_PCT  30.0f
#define AMBIENT_LUX_LOW_THRESHOLD   200.0f

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
    ESP_ERROR_CHECK(actuators_stepper_home());

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
            // control policy. TODO: each pot moisture should be individually tracked
            bool too_dry = (reading.soil_moisture_pct[0] < MOISTURE_LOW_THRESHOLD_PCT) ||
                            (reading.soil_moisture_pct[1] < MOISTURE_LOW_THRESHOLD_PCT) ||
                            (reading.soil_moisture_pct[2] < MOISTURE_LOW_THRESHOLD_PCT);
            actuators_set_pump(too_dry);

            bool too_dark = reading.ambient_lux < AMBIENT_LUX_LOW_THRESHOLD;
            actuators_set_led_brightness(too_dark ? 100 : 0);
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