#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*
actuators.kicad_sch of the schematic:
    - LED grow light (PWM dimmable, driven through Q2 NMOS)
    - Submersible mini-pump (on/off, driven through Q1 NMOS)
    - 28BYJ-48 stepper motor + ULN2003 driver
    - Homing / limit switch used to zero the stepper position
*/

typedef enum {
    STEPPER_DIR_CW = 0,
    STEPPER_DIR_CCW,
} stepper_dir_t;

// Configure GPIOs/LEDC for the LED, pump, stepper coils, and limit switch.
esp_err_t actuators_init(void);

// LED grow light brightness, 0-100.
esp_err_t actuators_set_led_brightness(uint8_t percent);

// Pump on/off.
esp_err_t actuators_set_pump(bool enabled);

// Stepper motor.
esp_err_t actuators_stepper_move(stepper_dir_t dir, uint32_t steps);

esp_err_t actuators_stepper_home(void);
bool      actuators_limit_switch_triggered(void);

#endif /* ACTUATORS_H */