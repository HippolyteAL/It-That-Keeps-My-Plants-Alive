# It That Keeps My Plants Alive

## Introduction

## Wokwi simulation

## Software architecture
- `sensor_task` — periodic sampling of moisture/temp/humidity/light
- `actuator_task` — hysteresis pump control + RTC-driven light schedule + stepper control
- `logging_task` — CSV to SD card and/or MQTT publish

(#TODO add diagram)

## Build

For VSC:
- Install ISP-IDF v5.5.5
- In an ESP-IDF terminal, run the commands:
    - idf.py set-target esp32
    - idf.py build

## Hardware

See the Schematic.pdf, here's a breakdown of materials (cost sourced from canadian prices):

Piece | Function | Cost (pre-tax, CAD)
--- | --- | ---
ESP32-WROOM32E | Main controller | 13$
28BYJ-48 stepper + ULN2003 driver | Motion control | 4$
2x Logic-level MOFSETs | Actuator activation | 2x5$
5V LED grow light | light | 15-20$
5V submersible pump | water | 12$
2x push switches | control | 2$
DS18B20 | Soil temperature sensor | 3x5$
3x Soil moisture sensor | Moisture sensor | 15$
SHT31 | Ambient temperature and humidity sensor | 12-18$
BH1750 | Light sensor | 7$
DS3231 | Power independant time tracking | 7$
MicroSD module + chip | Logging | 14$
3x 10kΩ resistor | Circuits | variable (bulk vs indiv.)
4.7kΩ resistor | Circuits | variable
1μF capacitor | Circuits | variable
Flyback Diode | Circuits | variable
TOTAL | | 137$ + electronics + tx

Tools (testing) | Importance | Cost (pre-tax)
--- | --- | ---
Breadboard kit | Essential | 20$
Extra breadboard | Useful | 8$
Multimeter | Variable | 25$
Perfboard | Essential | 2$
TOTAL | | 22-55$ + tx
