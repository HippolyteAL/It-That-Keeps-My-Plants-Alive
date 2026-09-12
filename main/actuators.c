#include "actuators.h"
#include "pinout.h"
#include "storage.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const char *TAG = "actuators";

// Setup ------------------------------------------------------------------------------------------

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_LED    LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT           // 0-1023
#define LEDC_FREQUENCY_HZ   5000
#define LEDC_DUTY_MAX       ((1 << LEDC_DUTY_RES) - 1)

// 10% = 1000 lux, dummy values purely for simulation testing
#define LED_LUX_PER_PERCENT    100.0f                   

// 28BYJ-48 does 2048 steps for a full revolution, matching diagram.json's stepper gearRatio
#define STEPS_PER_REV           2048u
#define STEPPER_STEP_DELAY_MS   10   

// Arbitrary for the simulation
#define WATER_PULSE_MS          3000

// Column order matches PIN_STEPPER_COIL_{A_PLUS,A_MINUS,B_PLUS,B_MINUS}
static const uint8_t STEPPER_STEP_TABLE[4][4] = {
    {1, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 1},
    {1, 0, 0, 1},
};
static const gpio_num_t s_stepper_pins[4] = {
    PIN_STEPPER_COIL_A_PLUS, PIN_STEPPER_COIL_A_MINUS,
    PIN_STEPPER_COIL_B_PLUS, PIN_STEPPER_COIL_B_MINUS,
};
// Current position, always kept in [0, STEPS_PER_REV), zeroed by actuators_stepper_home()
static int32_t s_stepper_position   = 0;
// Tracks which row of STEPPER_STEP_TABLE is currently or was last powered
static uint8_t s_stepper_step_index = 0;

// Initialization ---------------------------------------------------------------------------------

// LED grow light, PWM via LEDC, dimmable 0-100%
esp_err_t init_LED(void) {
    esp_err_t err;

    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_FREQUENCY_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }
 
    ledc_channel_config_t ledc_channel = {
        .gpio_num   = PIN_LED_PWM,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL_LED,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,                    // starts off
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    return err;
}

// Pump (LED in the wokwi simulation), digital on/off with a duration
esp_err_t init_PUMP(void) {
    esp_err_t err;

    gpio_config_t pump_conf = {
        .pin_bit_mask = 1ULL << PIN_PUMP_EN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&pump_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pump gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(PIN_PUMP_EN, 0);

    return err;
}

// Limit switch, only makes sense to implement further outside the simulation.
esp_err_t init_limit_switch(void) {
    esp_err_t err;

    gpio_config_t limit_conf = {
        .pin_bit_mask = 1ULL << PIN_LIMIT_SW,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&limit_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "limit switch gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    return err;
}

// Stepper coils, 4 digital outputs initialized unpowered.
esp_err_t init_stepper(void) {
    esp_err_t err;

    gpio_config_t stepper_conf = {
        .pin_bit_mask = (1ULL << PIN_STEPPER_COIL_A_PLUS)  | (1ULL << PIN_STEPPER_COIL_A_MINUS) |
                        (1ULL << PIN_STEPPER_COIL_B_PLUS)  | (1ULL << PIN_STEPPER_COIL_B_MINUS),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&stepper_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stepper gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    for (int i = 0; i < 4; i++) {
        gpio_set_level(s_stepper_pins[i], 0);
    }
    
    return err;
}

esp_err_t actuators_init(void) {
    ESP_LOGI(TAG, "actuators_init");

    esp_err_t err;

    err = init_LED();
    if (err != ESP_OK) {
        return err;
    }

    err = init_PUMP();
    if (err != ESP_OK) {
        return err;
    }

    err = init_limit_switch();
    if (err != ESP_OK) {
        return err;
    }

    err = init_stepper();
    if (err != ESP_OK) {
        return err;
    }

    return err;
}

esp_err_t actuators_set_led_brightness(uint8_t percent) {
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err;
    uint32_t duty = ((uint32_t)percent * LEDC_DUTY_MAX) / 100;
    
    err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_LED, duty);
    if (err != ESP_OK) {
        return err;
    }

    err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_LED);
    if (err != ESP_OK) {
        storage_log_error("actuators_set_led_brightness/update_duty", err);
    }
    return err;
}

esp_err_t actuators_set_led_for_target_lux(float ambient_lux, float target_lux, uint8_t *out_percent) {
    float deficit = target_lux - ambient_lux;
    float percent_f = deficit / LED_LUX_PER_PERCENT;
    
    if (percent_f < 0.0f) {
        percent_f = 0.0f;
    } else if (percent_f > 100.0f) {
        percent_f = 100.0f;
    }
    
    uint8_t percent = (uint8_t)(percent_f + 0.5f);  // rounding to nearest
    
    esp_err_t err = actuators_set_led_brightness(percent);
    if (err == ESP_OK && out_percent != NULL) {
        *out_percent = percent;
    }
    return err;
}

esp_err_t actuators_set_pump(bool enabled) {
    esp_err_t err = gpio_set_level(PIN_PUMP_EN, enabled ? 1 : 0);
    if (err != ESP_OK) {
        storage_log_error("actuators_set_pump", err);
    }
    return err;
}

esp_err_t actuators_stepper_move(stepper_dir_t dir, uint32_t steps) {
    for (uint32_t i = 0; i < steps; i++) {
        if (dir == STEPPER_DIR_CW) {
            s_stepper_step_index = (uint8_t)((s_stepper_step_index + 1) % 4);
            s_stepper_position   = (s_stepper_position + 1) % (int32_t)STEPS_PER_REV;

            for (int p = 0; p < 4; p++) {
                gpio_set_level(s_stepper_pins[p], STEPPER_STEP_TABLE[s_stepper_step_index][p]);
            }
        } 
        // TODO: fix CCW direction
        if (dir == STEPPER_DIR_CCW) {
            s_stepper_step_index = (uint8_t)((s_stepper_step_index + 3) % 4);
            s_stepper_position   = (s_stepper_position + (int32_t)STEPS_PER_REV - 1) % (int32_t)STEPS_PER_REV;

            for (int p = 0; p < 4; p++) {
                gpio_set_level(s_stepper_pins[3-p], STEPPER_STEP_TABLE[s_stepper_step_index][3-p]);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(STEPPER_STEP_DELAY_MS));
    }
    
    // Unpower between moves
    for (int p = 0; p < 4; p++) {
        gpio_set_level(s_stepper_pins[p], 0);
    }

    return ESP_OK;
}
 
esp_err_t actuators_stepper_home(void) {
    ESP_LOGI(TAG, "control_task: started homing sequence.");
    /* Step one direction until the limit switch trips, or after one full revolution (for simulation only) TODO: edit before moving to hardware,
    and make it move opposite the watering event movement. */
    bool found = false;
    for (uint32_t i = 0; i < STEPS_PER_REV; i++) {
        if (actuators_limit_switch_triggered()) {
            found = true;
            break;
        }
        // TODO: remove this clause outside of simulation testing (no physical homing button)
        if (s_stepper_position == 0) {
            found = true;
            break;
        }
        actuators_stepper_move(STEPPER_DIR_CCW, abs(s_stepper_position));
    }
    
    if (!found) {
        ESP_LOGW(TAG, "stepper_home: limit switch never triggered after a full revolution");
        storage_log_error("actuators_stepper_home", ESP_ERR_NOT_FOUND);
    }

    ESP_LOGI(TAG, "control_task: stepper position succesfully reset.");

    s_stepper_position = 0;
    s_stepper_step_index = 0;
    return ESP_OK;
}
 
bool actuators_limit_switch_triggered(void) {
    return gpio_get_level(PIN_LIMIT_SW) == 0;
}

esp_err_t actuators_water_zone(uint8_t zone_index, int64_t unix_time) {
    if (zone_index > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Testing-only positions: 1/6, 2/6, 3/6 of a full rotation from home
    int32_t target_steps = ((int32_t)(zone_index + 1) * (int32_t)STEPS_PER_REV) / 6;
    
    // TODO: this makes it always move in the same direction outside of the homing sequence, change?
    int32_t delta = target_steps - s_stepper_position;
    if (delta < 0) {
        delta += (int32_t)STEPS_PER_REV;
    }
 
    esp_err_t err = actuators_stepper_move(STEPPER_DIR_CW, (uint32_t)delta);
    if (err != ESP_OK) {
        storage_log_error("actuators_water_zone/stepper_move", err);
        return err;
    }
 
    // TODO: implement controls for a pump, not an LED before moving from sim to hardware
    err = actuators_set_pump(true);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(WATER_PULSE_MS));
 
    err = actuators_set_pump(false);
    if (err != ESP_OK) {
        return err;
    }
 
    // TODO: measure volume, not doable/worth it to implement in a simumation.
    storage_log_water_event(unix_time, zone_index, -1.0f);

    // Return the stepper to initial position
    err = actuators_stepper_home();
    if (err != ESP_OK) {
        return err;
    }
 
    return ESP_OK;
}