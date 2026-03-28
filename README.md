# 💡 Patchworks

This project consists of a set of custom electronics prototyping boards designed for testing and monitoring personal electronics projects. Each board features:

- An MCU controlled LCD display.
- Voltage, current, power (V/A/W), and ambient temperature gauges.
- Regulated 3.3V, 5V, and VCC rails (7-12 VDC) for the dual 830 tie-point breadboards.
- A main ATmega328P Arduino Uno R3 for system monitoring (e.g., temperature and fan control for the main regulator, and power safety with the main relay).

> If you found this project useful, interesting, or worth keeping an eye on, consider giving it a ⭐️.
> It helps others discover the project and motivates me to keep building and sharing more.

## 🔹 Ideas & Upcoming Changes

- Change to fixed-point arithmetic.
- Contain related variables in structs or convert to fully object-oriented to encapsulate fixed point arithmetic, LCD functions, and chip setup, among others.
- Doxygen documentation.

## 🔹 Rev 1.1 Schematic

![Rev 1.1](<Schematics/Rev 1.1.png>)

## 🔹 V1.4.0 Firmware For Rev 1.1 Schematic

- Voltage low/high counts + shutdown.
- Refactoring.

## 🔹 V1.3.0 Firmware For Rev 1.1 Schematic

- MCU supervised low/high voltage detection for all voltage rails.
- Hardware overvoltage detection and automatic shutdown via [Snubby](https://github.com/Broosky/Snubby).
- USB-A power output.
- MCU & manual control for the regulator fan.
- Refine allowed input voltages.
- Move fuse from GND to VIN.
- Additional flyback diode on the regulator fan.
- Remove random determinism.
- Firmware review, refactoring, and clean-up.

## 🔹 Rev 1 Construction

![Patchworks Rev 1](<Construction/Patchworks Rev 1.jpg>)

## 🔹 V1.2.0 Firmware For Rev 1 Schematic

- Track fan cycles, store error code in EEPROM + interrogate on startup.

## 🔹 V1.1.1 Firmware For Rev 1 Schematic

- Helpers, separations, and general review.

## 🔹 V1.0.0 Firmware For Rev 1 Schematic

- Initial release. No functional changes.

## 🔹 General Operation

- Only one power source should be used at a time (batteries, DC input, or other connections like alligator clips, etc.).
  - It is safe to power the Arduino units via USB, regardless of which external power source is selected.

- A pushbutton is used to:
  - Initiate a POST (Power-On Self-Test).
  - Verify the regulated VCC before engaging the power-on latch.

- Holding the button after the POST completes will engage the power-on latch.
- The power-on latch is managed by the main Arduino Uno and/or the breadboard electronics, allowing shutdown automatically under certain fault conditions (temperature, inefficient cooling, overvoltage).

## 🔹 Other Repositories

- Front row (to back):
  - [7Driver](https://github.com/Broosky/7Driver)
  - [Squarely](https://github.com/Broosky/Squarely)
  - Two Patchworks (this repo)

- Left side:
  - [Wattson](https://github.com/Broosky/Wattson)

- Right side:
  - Christmas tree

![Bench](Bench.jpg)

##

> Educational Use Notice: This project is provided for educational and learning purposes only. You are welcome to read, study, and experiment
> with this software and/or hardware. It is not intended for commercial use. This software and/or hardware is provided "as is", without warranty
> of any kind. The author assumes no responsibility for any damages or issues resulting from its use.