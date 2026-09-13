/* 
Author: Hippolyte Audet-Lagacé

Simulated DS3231 RTC for Wokwi. Implements the real DS3231 I2C protocol 
(fixed address 0x68, BCD-encoded registers 0x00-0x06 with auto-increment) 
so the existing ESP-IDF DS3231 driver code works unmodified on the simulated chip.

The seven sliders (hour/minute/second/date/month/year/day_of_week) seed the chip's starting time.
A repeating 1-second timer advances the clock forward automatically like a real RTC running freely.
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
    
    // These attribute handles are only needed here, to seed the starting time on initialization
    uint32_t attr_hour   = attr_init("hour", 12);
    uint32_t attr_minute = attr_init("minute", 0);
    uint32_t attr_second = attr_init("second", 0);
    uint32_t attr_date   = attr_init("date", 1);
    uint32_t attr_month  = attr_init("month", 1);
    uint32_t attr_year   = attr_init("year", 25);
    uint32_t attr_dow    = attr_init("day_of_week", 1);
    
    // Seed internal state from the sliders' current (default) values.
    chip->hour   = (int)attr_read(attr_hour);
    chip->minute = (int)attr_read(attr_minute);
    chip->second = (int)attr_read(attr_second);
    chip->date   = (int)attr_read(attr_date);
    chip->month  = (int)attr_read(attr_month);
    chip->year   = (int)attr_read(attr_year);
    chip->dow    = (int)attr_read(attr_dow);
    
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
    timer_start(chip->timer, 1000000, true);  // 1,000,000 \mu s = 1 second, repeating
    
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
        case 0x02: return dec_to_bcd(chip->hour);  // 24-hour mode, I think the chip has both options
        case 0x03: return dec_to_bcd(chip->dow);
        case 0x04: return dec_to_bcd(chip->date);
        case 0x05: return dec_to_bcd(chip->month);
        case 0x06: return dec_to_bcd(chip->year);
        default:   return 0x00;
    }
}

// Advances the clock by exactly one second per second :o
void tick(void *user_data) {
    chip_state_t *chip = (chip_state_t *)user_data;
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