/* 
Simulated DS3231 RTC for Wokwi. Implements the real DS3231 I2C protocol 
(fixed address 0x68, BCD-encoded registers 0x00-0x06 with auto-increment) 
so the existing ESP-IDF DS3231 driver code works unmodified on the simulated chip.

The seven sliders (hour/minute/second/date/month/year/day_of_week) seed the chip's starting time.
A repeating 1-second timer advances the clock forward automatically like a real RTC running freely.
Dragging a slider mid-simulation allows adjusting the internal clock to the new value for testing.
This kind of adjustment cannot be done on real hardware as the chip is read-only.
*/

#include "wokwi-api.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define DS3231_I2C_ADDRESS 0x68
#define NUM_REGS 7  // seconds, minutes, hours, day-of-week, date, month, year

typedef struct {
    pin_t scl;
    pin_t sda;
    i2c_dev_t i2c;
    timer_t timer;
    // Slider attribute IDs
    uint32_t attr_hour;
    uint32_t attr_minute;
    uint32_t attr_second;
    uint32_t attr_dow;
    uint32_t attr_date;
    uint32_t attr_month;
    uint32_t attr_year;
    // Internal running clock state (BCD conversion on send)
    int second, minute, hour, dow, date, month, year;
    // I2C transaction state (next register returned)
    uint8_t reg_ptr;
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool connect);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void tick(void *user_data);

void chip_init(void) {
    chip_state_t *chip = calloc(1, sizeof(chip_state_t));
    
    chip->scl = pin_init("SCL", INPUT_PULLUP);
    chip->sda = pin_init("SDA", INPUT_PULLUP);
    
    chip->attr_hour   = attr_init("hour", 12);
    chip->attr_minute = attr_init("minute", 0);
    chip->attr_second = attr_init("second", 0);
    chip->attr_date   = attr_init("date", 1);
    chip->attr_month  = attr_init("month", 1);
    chip->attr_year   = attr_init("year", 25);
    chip->attr_dow    = attr_init("day_of_week", 1);
    
    // Seed internal state from the sliders' current (default) values.
    chip->hour   = (int)attr_read(chip->attr_hour);
    chip->minute = (int)attr_read(chip->attr_minute);
    chip->second = (int)attr_read(chip->attr_second);
    chip->date   = (int)attr_read(chip->attr_date);
    chip->month  = (int)attr_read(chip->attr_month);
    chip->year   = (int)attr_read(chip->attr_year);
    chip->dow    = (int)attr_read(chip->attr_dow);
    
    const i2c_config_t i2c_config = {
        .user_data = chip,
        .address = DS3231_I2C_ADDRESS,
        .scl = chip->scl,
        .sda = chip->sda,
        .connect = on_i2c_connect,
        .read = on_i2c_read,
        .write = on_i2c_write,
    };
    chip->i2c = i2c_init(&i2c_config);
    
    const timer_config_t timer_config = {
        .callback = tick,
        .user_data = chip,
    };
    chip->timer = timer_init(&timer_config);
    timer_start(chip->timer, 1000000, true);  // 1,000,000 us = 1 second, repeating
    
    printf("DS3231 simulator ready -- starting at %02d:%02d:%02d\n", 
        chip->hour, chip->minute, chip->second);
}

static uint8_t dec_to_bcd(int val) {
  return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static int days_in_month(int month, int year) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4 == 0)) return 29;  // leap-year-ish
    return days[month - 1];
}
 
static uint8_t read_register(chip_state_t *chip, uint8_t reg) {
    switch (reg) {
        case 0x00: return dec_to_bcd(chip->second);
        case 0x01: return dec_to_bcd(chip->minute);
        case 0x02: return dec_to_bcd(chip->hour);  // 24-hour mode, i think the chip has both options
        case 0x03: return dec_to_bcd(chip->dow);
        case 0x04: return dec_to_bcd(chip->date);
        case 0x05: return dec_to_bcd(chip->month);
        case 0x06: return dec_to_bcd(chip->year);
        default:   return 0x00;
    }
}

void tick(void *user_data) {
    chip_state_t *chip = (chip_state_t *)user_data;
    
    int new_hour   = (int)attr_read(chip->attr_hour);
    int new_minute = (int)attr_read(chip->attr_minute);
    int new_second = (int)attr_read(chip->attr_second);
    int new_date   = (int)attr_read(chip->attr_date);
    int new_month  = (int)attr_read(chip->attr_month);
    int new_year   = (int)attr_read(chip->attr_year);
    int new_dow    = (int)attr_read(chip->attr_dow);
    
    bool slider_moved = (new_hour != chip->hour || new_minute != chip->minute || new_date != chip->date
        || new_month != chip->month || new_year != chip->year || new_dow != chip->dow);
    if (slider_moved) {
        // Live override: snap to the slider values, then keep ticking from here.
        chip->hour = new_hour;
        chip->minute = new_minute;
        chip->second = new_second;
        chip->date = new_date;
        chip->month = new_month;
        chip->year = new_year;
        chip->dow = new_dow;
        return;
    }
    // Normal tick: advance one second and roll over as needed.
    chip->second++;
    if (chip->second >= 60) {
        chip->second = 0;
        chip->minute++;
        if (chip->minute >= 60) {
            chip->minute = 0;
            chip->hour++;
            if (chip->hour >= 24) {
                chip->hour = 0;
                chip->dow = (chip->dow % 7) + 1;
                chip->date++;
                if (chip->date > days_in_month(chip->month, chip->year)) {
                    chip->date = 1;
                    chip->month++;
                    if (chip->month > 12) {
                        chip->month = 1;
                        chip->year = (chip->year + 1) % 100;
                    }
                }
            }
        }
    }
}

// Simulated hardware operation (I2C) -------------------------------------------------------------

bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
    return true;
}

// Cycles through registers with auto-increment, matching the real DS3231.
uint8_t on_i2c_read(void *user_data) {
    chip_state_t *chip = (chip_state_t *)user_data;
    uint8_t value = read_register(chip, chip->reg_ptr);
    chip->reg_ptr = (chip->reg_ptr + 1) % NUM_REGS;
    return value;
}
 
bool on_i2c_write(void *user_data, uint8_t data) {
    return true;
}