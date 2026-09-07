#include "actuators.h"
#include "pinout.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const char *TAG = "actuators";

// Running step position, zeroed by actuators_stepper_home().
static int32_t s_stepper_position = 0;

// LED setup --------------------------------------------------------------------------------------

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_LED    LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT   /* 0-1023 */
#define LEDC_FREQUENCY_HZ   5000
#define LEDC_DUTY_MAX       ((1 << LEDC_DUTY_RES) - 1)

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

    return ESP_OK;
}

// Initialization ---------------------------------------------------------------------------------

esp_err_t actuators_init(void) {
    ESP_LOGI(TAG, "actuators_init");

    esp_err_t err;

    // TODO: gpio_config() PIN_PUMP_EN as output, default level 0 (off)
    // TODO: gpio_config() PIN_LIMIT_SW as input with internal pull-up
    // TODO: gpio_config() the 4 stepper coil pins as outputs, default low
    // TODO: ledc_timer_config() + ledc_channel_config() on PIN_LED_PWM using LEDC_TIMER / LEDC_MODE / LEDC_CHANNEL_LED above

    err = init_LED();
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t actuators_set_led_brightness(uint8_t percent) {
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t duty = ((uint32_t)percent * LEDC_DUTY_MAX) / 100;
 
    esp_err_t err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_LED, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_LED);
}

esp_err_t actuators_set_pump(bool enabled) {
    // TODO: gpio_set_level(PIN_PUMP_EN, enabled ? 1 : 0)
    (void)enabled;
    return ESP_OK;
}
 
esp_err_t actuators_stepper_move(stepper_dir_t dir, uint32_t steps) {
    /* TODO: drive the 4 coil GPIOs (PIN_STEPPER_COIL_*) through the wave/full/half-step sequence for "steps" iterations, direction
    given by "dir", with a short vTaskDelay() between steps to respect the 28BYJ-48's max step rate. Update s_stepper_position as it goes. 
    */
    (void)dir;
    (void)steps;
    return ESP_OK;
}
 
esp_err_t actuators_stepper_home(void) {
    /* TODO: step slowly in one direction until actuators_limit_switch_triggered() reads true, then zero
    s_stepper_position. Called once at boot from app_main(), and again whenever SW_Push (the "sanity check" button) is pressed. */
    s_stepper_position = 0;
    return ESP_OK;
}
 
bool actuators_limit_switch_triggered(void) {
    // TODO: gpio_get_level(PIN_LIMIT_SW) - wired active-low (pressed == 0)
    return false;
}