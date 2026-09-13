# It That Keeps My Plants Alive

## Introduction

My houseplants met their untimely demise to the hands of not receiving the proper amount of water, so I have come up with a solution. It's a machine that monitors temperature, humidity and light and controls a grow light and water arm to make sure the plants stay alive. There are also multiple extra sensors to gather various data on ambient humidity/temperature and soil temperature that are unnecessary for the actuator, but I wanted to make something that also gathers data for fun.

## Wokwi simulation

Before getting into any expenses, I decided to run this whole thing in a Wokwi simulation  linked here:

https://wokwi.com/projects/475088516800323585

To run the simulation, download the it_that_keeps_my_plants_alive.bin file from the repo and use the command "F1 -> Upload Firmware and Start Simulation" , uploading the .bin file into the browser.

The soil moisture sensors are replaced with potentiometers in the simulation, so the can be moved around to make the machine water different positions. The downside is that they arent automatically updated when watered and as such are watered repeatedly until manually raised above 30% moisture. All the numerical values are arbitrary placeholders, because real numbers would depend on the real plants being watered. Also the pump is an LED, because it is more visual for a simulation.

This also serves as a good way to picture what this machine is.


## Software architecture
Located in main/, the schematic.pdf pages mirror the structure of the code files.

File | Purpose
---|---
main.c | Main
sensors.c | Periodic sampling of moisture/temp/humidity/light
actuators.c | Pump control + RTC-driven light schedule + stepper control
storage.c | CSV to SD card

## Build

- Install ISP-IDF v5.5.5
- In an ESP-IDF terminal, run the commands:
    - idf.py set-target esp32
    - idf.py build

## Hardware

See the Schematic.pdf, here's an estimate of materials cost (sourced from canadian prices, 2026):

Piece | Function | Cost (pre-tax, CAD)
--- | --- | ---
ESP32-WROOM32E | Main controller | 13$
28BYJ-48 stepper + ULN2003 driver | Motion control | 4$
2x Logic-level MOFSETs | Actuator activation | 2x5$
5V LED grow light | light | 10-20$
5V submersible pump | water | 12$
push switch | control | 1$
DS18B20 | Soil temperature sensor | 15$
3x Soil moisture sensor | Moisture sensor | 3x5$
SHT31 | Ambient temperature and humidity sensor | 12-18$
BH1750 | Light sensor | 7$
DS3231 | Power independant time tracking | 7$
MicroSD module + chip | Logging | 14$
3x 10kΩ resistor | Circuits | variable (bulk vs indiv.)
4.7kΩ resistor | Circuits | variable
1μF capacitor | Circuits | variable
Flyback Diode | Circuits | variable
TOTAL | | ~130 $CAD + electronics + tx

Tools (testing) | Importance | Cost (pre-tax)
--- | --- | ---
Breadboard kit | Essential | 20$
Extra breadboard | Useful | 8$
Multimeter | Variable | 25$
Perfboard | Essential | 2$
TOTAL | | 22-55 $CAD + tx

The total can be brought under 100 $CAD easily if you borrow / already have appropriate tools (or above the current estimate if you choose a more permanent/long term custom circuit board). You can also cut ~50 $CAD by bailing on the purely data gathering part of the project (MicroSD, SHT31, DS18B20).
