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

NOTE: these are testing-phase implementations for the Wokwi simulation, not calibrated for real hardware/plants yet:
    - In diagram.json the pump is a plain LED with no fluid or moisture feedback, so actuators_water_zone() just pulses it, 
    it has no effect on the moisture potentiometers.
    - The stepper's per-zone target positions (1/6, 2/6, 3/6 of a full rotation) are arbitrary placeholders, not measured 
    against any real mechanism.
    - The grow light's lux contribution (see LED_LUX_PER_PERCENT in actuators.c) is an arbitrary round number for easy testing, 
    not a real photometric value.
*/

typedef enum {
    STEPPER_DIR_CW = 0,
    STEPPER_DIR_CCW,
} stepper_dir_t;

// Configure GPIOs/LEDC for the LED, pump, stepper coils, and limit switch.
esp_err_t actuators_init(void);

// LED grow light brightness, 0-100.
esp_err_t actuators_set_led_brightness(uint8_t percent);
// Brings ambient luminosity to a certain target
esp_err_t actuators_set_led_for_target_lux(float ambient_lux, float target_lux, uint8_t *out_percent);

// Pump on/off.
esp_err_t actuators_set_pump(bool enabled);

// Stepper motor.
esp_err_t actuators_stepper_move(stepper_dir_t dir, uint32_t steps);
esp_err_t actuators_stepper_home(void);
bool      actuators_limit_switch_triggered(void);
// Zone 0/1/2
esp_err_t actuators_water_zone(uint8_t zone_index, int64_t unix_time);

#endif /* ACTUATORS_H */