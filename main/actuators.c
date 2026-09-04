#include "actuators.h"
#include "pinout.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const char *TAG = "actuators";

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_LED    LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT   /* 0-1023 */
#define LEDC_FREQUENCY_HZ   5000

// Running step position, zeroed by actuators_stepper_home().
static int32_t s_stepper_position = 0;

esp_err_t actuators_init(void) {
    ESP_LOGI(TAG, "actuators_init: stub");
    // TODO: gpio_config() PIN_PUMP_EN as output, default level 0 (off)
    // TODO: gpio_config() PIN_LIMIT_SW as input with internal pull-up
    // TODO: gpio_config() the 4 stepper coil pins as outputs, default low
    // TODO: ledc_timer_config() + ledc_channel_config() on PIN_LED_PWM using LEDC_TIMER / LEDC_MODE / LEDC_CHANNEL_LED above
    return ESP_OK;
}

esp_err_t actuators_set_led_brightness(uint8_t percent) {
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    // TODO: scale percent to the LEDC_DUTY_RES range, ledc_set_duty() + ledc_update_duty() on LEDC_CHANNEL_LED.
    return ESP_OK;
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