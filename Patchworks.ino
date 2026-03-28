/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Project: Patchworks                                                                                                     //
// Author: Jeffrey Bednar                                                                                                  //
// Copyright (c) Illusion Interactive, 2011 - 2026.                                                                        //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Educational Use Notice:                                                                                                 //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This project is provided for educational and learning purposes only. You are welcome to read, study, and experiment     //
// with this software and/or hardware. It is not intended for commercial use. This software and/or hardware is provided    //
// "as is", without warranty of any kind. The author assumes no responsibility for any damages or issues resulting from    //
// its use.                                                                                                                //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Date: Tuesday, October 3rd, 2023
// Description: Source for the Patchworks electronics prototyping and monitoring board.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fold all: Ctrl + K + 0
// Unfold all: Ctrl + K + J
// Show file explorer: Ctrl + Shift + E
// Auto format: Ctrl + T
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "Headers/fixed_point.h"
#include "Headers/lcd.h"
#include "Headers/melody.h"
#include <EEPROM.h>  // 1024 bytes available, addresses: 0 - 1023, width: 8 bits. Degrades.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern LiquidCrystal_I2C lcd;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Firmware version:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t FW_VERSION_MAJOR = 1;
const uint8_t FW_VERSION_MINOR = 4;
const uint8_t FW_VERSION_PATCH = 0;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Types:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef enum ERROR_CODE : uint8_t {  // Align with ERROR_CODE_NAMES.
  ERROR_CODE_NONE,                   // No error, ignore.
  ERROR_CODE_INVALID,                // Invalid
  ERROR_CODE_HOT_CYCLES,             // Too many cycles at or above maximum temperature.
  ERROR_CODE_COOLDOWN,               // Expected cooling duration exceeded. This could also trigger if the 5V rail sags. The 5V
                                     // (and/or the 3V3) rail coud sag due to a bad output capacitor or even a failing regulator.
                                     // As such, if the fan is triggered under normal operation the 5V rail can't supply enough power
                                     // to it, so the cooldown duration can exceed the threshold.
  ERROR_CODE_BREADBOARD,             // The breadboard circuitry is commanding the MCU to shutdown the power.
  ERROR_CODE_VOLTAGE_HIGH_COUNT,     // Too many cycles for a rail to be at high voltage.
  ERROR_CODE_VOLTAGE_LOW_COUNT,      // Too many cycles for a rail to be at low voltage.
  ERROR_CODE_COUNT                   // Must be last; for iterating/bounds.
} ERROR_CODE_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct F_EXTREMA {
  float min;
  float max;
} F_EXTREMA_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct I32_EXTREMA {
  int32_t min;
  int32_t max;
} I32_EXTREMA_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct UI32_EXTREMA {
  uint32_t min;
  uint32_t max;
} UI32_EXTREMA_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program constants:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t PIN_OUTPUT_BUZZER = 8;
const uint8_t PIN_OUTPUT_FAN_ENABLE = 9;
const uint8_t PIN_OUTPUT_RELAY_ENABLE = 6;
const uint8_t PIN_OUTPUT_PROGRAM_ACTIVE = 2;
const uint8_t PIN_OUTPUT_EXTERNAL_CLOCK = 10;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t PIN_INPUT_VCC_SENSE = A1;
const uint8_t PIN_INPUT_5V_SENSE = A2;
const uint8_t PIN_INPUT_3V3_SENSE = A3;
const uint8_t PIN_INPUT_THERMISTOR_SENSE = A0;
const uint8_t PIN_INPUT_BREADBOARD_ERROR_SENSE = 11;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t VOLTAGE_READ_SAMPLES = 16;
const float VOLTAGE_BANDGAP_REFERENCE = 1.1;  // Should be measured per MCU as this varies.
const float VOLTAGE_DIVIDER_R1 = 47000.0;     // Common across VCC, 5V, and 3V3 voltage rails (1% tolerance). Allows safe input readings
                                              // for comparison with the bandgap if VCC reaches up to 24V. Although the
                                              // Arduino regulator supports up to 12V, this provides additional survivability
                                              // in the event if the main VCC regulator fails or there's a rail short. Regardless, the
                                              // hardware overvoltage protection will trigger at approximately 12.7V, 5.8V,
                                              // and 4.0V for each respective rail.
const float VOLTAGE_DIVIDER_R2 = 2000.0;
const uint8_t VOLTAGE_LOW_MAXIMUM_CYCLE_COUNT = 5;
const uint8_t VOLTAGE_HIGH_MAXIMUM_CYCLE_COUNT = 5;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const float TEMP_ZERO_KELVIN = 273.15;
const float TEMP_LOWERBOUND = 37.00;                   // Celcius: fan off, single chirp.
const float TEMP_UPPERBOUND = 41.00;                   // Celcius: fan on until lowerbound is reached, multiple chirps.
const float TEMP_MAXIMUM = 50.0;                       // Celcius: fan on, different chirp tone. If enough cycles at or above
                                                       // this temperature, the board will automatically shut down.
const uint8_t TEMP_MAXIMUM_CYCLE_COUNT = 5;            // Number of cycles above maximum temperature before auto shut down.
const uint8_t TEMP_COOLDOWN_MAX_MINUTES = 3;           // Number of minutes to allow cooling to the lowerbound temperature
                                                       // before automatically shutting down.
const uint8_t THERMISTOR_NOMINAL_TEMPERATURE = 25;     // Almost always 25 degrees C; check datasheet.
const uint8_t THERMISTOR_READ_SAMPLES = 16;            // How many times the voltage is read before deciding an average value.
const uint16_t THERMISTOR_NOMINAL_RESISTANCE = 10000;  // Ohms
const uint16_t THERMISTOR_BETA = 3950;                 // Datasheet
const uint16_t THERMISTOR_REFERENCE = 10000;           // Ohms; resistor in the voltage divider.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint16_t DELAY_LOOP_COMPLETED = 750;  // MS
const uint16_t DELAY_LCD_PAGE_CYCLE = 1850;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const int32_t EXTERNAL_CLOCK_LOWERBOUND = 0;
const int32_t EXTERNAL_CLOCK_UPPERBOUND = 99;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* ERROR_CODE_NAMES[] = {
  "None",  // Align with ERROR_CODE.
  "Invalid",
  "Hot Cycles",
  "Cooldown",
  "Breadboard",
  "Vlt H Count",
  "Vlt L Count"
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const F_EXTREMA_T thresholdsMcu = { .min = 4.25, .max = 5.75 };
const F_EXTREMA_T thresholdsVcc = { .min = 11.25, .max = 12.75 };
const F_EXTREMA_T thresholds5V = { .min = 4.25, .max = 5.75 };
const F_EXTREMA_T thresholds3V3 = { .min = 2.8, .max = 3.7 };
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program globals and counters:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint16_t fanCycleCount = 0;                                       // Cumulative, never resets.
uint8_t tempHighCycleCount = 0;                                   // Cumulative, never resets.
F_EXTREMA_T voltageMcuExtrema = { .min = 999.9, .max = -999.9 };  // Inverted to normalize during runtime.
F_EXTREMA_T voltageVccExtrema = { .min = 999.9, .max = -999.9 };
F_EXTREMA_T voltage5VExtrema = { .min = 999.9, .max = -999.9 };
F_EXTREMA_T voltage3V3Extrema = { .min = 999.9, .max = -999.9 };
F_EXTREMA_T tempLifetimeExtrema = { .min = 999.9, .max = -999.9 };
I32_EXTREMA_T externalClockLifetimeExtrema = { .min = EXTERNAL_CLOCK_UPPERBOUND << 1, .max = -(EXTERNAL_CLOCK_UPPERBOUND << 1) };
UI32_EXTREMA_T voltageMcuThresholdCounts;
UI32_EXTREMA_T voltageVccThresholdCounts;
UI32_EXTREMA_T voltage5VThresholdCounts;
UI32_EXTREMA_T voltage3V3ThresholdCounts;
UI32_EXTREMA_T cooldownDurationLifetimeMsExtrema = { .min = UINT32_MAX, .max = 0 };
uint8_t showSplashScreen = true;
uint8_t showLastKnownError = true;
uint32_t loopCount = 0;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program initialization.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup(void) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Ideally the internal bandgap reference is configured once on setup, but due to imported libraries and
  // other code there is contention and will need to be explicitly set prior to measurements.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Set pin modes.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Outputs:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode(PIN_OUTPUT_PROGRAM_ACTIVE, OUTPUT);
  pinMode(PIN_OUTPUT_BUZZER, OUTPUT);
  pinMode(PIN_OUTPUT_FAN_ENABLE, OUTPUT);
  pinMode(PIN_OUTPUT_RELAY_ENABLE, OUTPUT);
  pinMode(PIN_OUTPUT_EXTERNAL_CLOCK, OUTPUT);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Inputs:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode(PIN_INPUT_VCC_SENSE, INPUT);
  pinMode(PIN_INPUT_5V_SENSE, INPUT);
  pinMode(PIN_INPUT_3V3_SENSE, INPUT);
  pinMode(PIN_INPUT_THERMISTOR_SENSE, INPUT);
  pinMode(PIN_INPUT_BREADBOARD_ERROR_SENSE, INPUT_PULLUP);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Init LCD.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  lcd.init();
  lcd.clear();
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  setupSeed();
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Test LCD, buzzer, fan, and set power-on latch.
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  testLcd(3, 150, 25);
  testBuzzer(&melodyTest);
  testThermistorFan(3, 500);
  setPowerOnLatch(HIGH);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program loop.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop(void) {
  handleSplashScreen(DELAY_LCD_PAGE_CYCLE);
  handleLastKnownError(DELAY_LCD_PAGE_CYCLE, &melodyShutdown);
  handleLoopStart(DELAY_LCD_PAGE_CYCLE);
  handleBreadboardError();

  handleExternalClock(EXTERNAL_CLOCK_LOWERBOUND,
                      EXTERNAL_CLOCK_UPPERBOUND,
                      0,
                      DELAY_LCD_PAGE_CYCLE);

  float currentTemperature = handleThermistor(true,
                                              true,
                                              DELAY_LCD_PAGE_CYCLE);
  handleThermistorFan(currentTemperature,
                      TEMP_LOWERBOUND,
                      TEMP_UPPERBOUND,
                      TEMP_MAXIMUM,
                      TEMP_COOLDOWN_MAX_MINUTES,
                      DELAY_LCD_PAGE_CYCLE);

  handleVoltageReadings(DELAY_LCD_PAGE_CYCLE);

  handleUptime(DELAY_LCD_PAGE_CYCLE);

  handleLoopEnd(DELAY_LOOP_COMPLETED);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Seed by mixing analog noise and runtime for a higher degree of entropy.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setupSeed(void) {
  unsigned long seed = 0;

  for (uint8_t i = 0; i < 32; i++) {
    for (uint8_t pin = A0; pin <= A5; pin++) {
      seed ^= analogRead(pin) << (i % 16);
    }
    seed ^= micros();
    delay(5);
  }

  randomSeed(seed);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check and warn if Snubby or other breadboard circuitry is reporting an error (active low).
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleBreadboardError(void) {
  uint8_t breadboardError = digitalRead(PIN_INPUT_BREADBOARD_ERROR_SENSE);

  if (!breadboardError) {
    shutdown(ERROR_CODE_BREADBOARD, NULL);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* const getErrorCodeName(ERROR_CODE_T errorCode) {
  if (errorCode >= ERROR_CODE_NONE && errorCode < ERROR_CODE_COUNT) {
    return ERROR_CODE_NAMES[errorCode];
  }
  return "UNKNOWN";
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generates a random number of deterministic clock cycles to be output to the external CC. For Squarely's U6 CP0 (pin 14).
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleExternalClock(int32_t rangeLowerbound, int32_t rangeUpperbound, uint8_t clockLengthMs, uint16_t lcdPageCycleDelayMs) {
  int32_t randomNumber = random(rangeLowerbound, rangeUpperbound + 1);

  if (randomNumber < externalClockLifetimeExtrema.min) {
    externalClockLifetimeExtrema.min = randomNumber;
  }
  if (randomNumber > externalClockLifetimeExtrema.max) {
    externalClockLifetimeExtrema.max = randomNumber;
  }

  if (randomNumber > 0) {
    for (int32_t i = 0; i < randomNumber; i++) {
      digitalWrite(PIN_OUTPUT_EXTERNAL_CLOCK, HIGH);
      if (clockLengthMs > 0) {
        delay(clockLengthMs);
      }
      digitalWrite(PIN_OUTPUT_EXTERNAL_CLOCK, LOW);
      if (i < randomNumber - 1) {
        if (clockLengthMs > 0) {
          delay(clockLengthMs);
        }
      }
    }
  }

  // Write the number and the lifetime min/max.
  printLabeledInt32(0, 0, "C: ", randomNumber, true, lcdPageCycleDelayMs);
  printLabeledInt32(0, 0, "Cmin: ", externalClockLifetimeExtrema.min, true, 0);
  printLabeledInt32(0, 1, "Cmax: ", externalClockLifetimeExtrema.max, false, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Engages the power-on latch to keep the board powered automatically.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setPowerOnLatch(uint8_t state) {
  digitalWrite(PIN_OUTPUT_RELAY_ENABLE, state);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cycles the power regulator cooling fan for test.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void testThermistorFan(uint8_t cycles, uint16_t fanCycleDelayMs) {
  for (uint8_t i = 0; i < cycles; i++) {
    digitalWrite(PIN_OUTPUT_FAN_ENABLE, HIGH);
    delay(fanCycleDelayMs);
    digitalWrite(PIN_OUTPUT_FAN_ENABLE, LOW);
    if (i < cycles - 1) {
      delay(1500);  // Let it come to a stop.
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plays a test tone for the buzzer.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void testBuzzer(const MELODY_T* const melody) {
  playMelody(melody);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plays a tone on the board's buzzer.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void playMelody(const MELODY_T* const melody) {
  for (uint8_t i = 0; i < melody->cycles; i++) {
    tone(PIN_OUTPUT_BUZZER, melody->frequency, melody->durationMs);
    if (i < melody->cycles - 1) {
      // Wait until the tone finishes.
      delay(melody->durationMs);
      delay(melody->toneCycleDelayMs);
    }
  }

  // Avoid buzzer chirp during other transistions.
  if (melody->pauseBeforeReturn) {
    delay(100);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main error handler.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void shutdown(ERROR_CODE_T errorCode, const uint32_t* const errorFlags) {
  // Playing the tone acts as the LCD page delay.
  printLabeledUInt8(0, 0, "E: ", errorCode, true, 0);
  printLabeledString(0, 1, "E: ", getErrorCodeName(errorCode), false, 0);

  size_t address = 0;
  EEPROM.put(address, errorCode);
  address += sizeof(errorCode);

  if (errorFlags) {
    EEPROM.put(address, *errorFlags);
    address += sizeof(*errorFlags);
  }

  playMelody(&melodyShutdown);
  delay(5000);

  // Self-terminate.
  setPowerOnLatch(LOW);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Displays a splash screen on the LCD.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleSplashScreen(uint16_t lcdPageCycleDelayMs) {
  if (showSplashScreen) {
    char version[16], date[16], time[16];

    printString(0, 0, "Illusion", true, 0);
    printString(0, 1, "Interactive", false, lcdPageCycleDelayMs);

    snprintf(version, sizeof(version), "v%" PRIu8 ".%" PRIu8 ".%" PRIu8, FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);

    printString(0, 0, "Patchworks", true, 0);
    printString(0, 1, version, false, lcdPageCycleDelayMs);

    snprintf(date, sizeof(date), "%s", __DATE__);
    snprintf(time, sizeof(date), "%s", __TIME__);

    printString(0, 0, date, true, 0);
    printString(0, 1, time, false, lcdPageCycleDelayMs);

    showSplashScreen = false;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the last known error on startup if there was an automatic fault shutdown.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleLastKnownError(uint16_t lcdPageCycleDelayMs, const MELODY_T* const melody) {
  if (showLastKnownError) {
    ERROR_CODE_T errorCode;
    uint32_t errorFlags;
    size_t address = 0;

    EEPROM.get(address, errorCode);
    address += sizeof(errorCode);

    if (errorCode > ERROR_CODE_NONE) {
      EEPROM.get(address, errorFlags);
      address += sizeof(errorFlags);

      printLabeledUInt8(0, 0, "El: ", errorCode, true, 0);
      printLabeledString(0, 1, "El: ", getErrorCodeName(errorCode), false, 0);

      // Alert in case we're not looking at the LCD.
      playMelody(melody);
      delay(lcdPageCycleDelayMs);

      // Reset
      EEPROM.put(0, ERROR_CODE_NONE);
    }

    showLastKnownError = false;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles actions at the loop start.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleLoopStart(uint16_t lcdPageCycleDelayMs) {
  digitalWrite(PIN_OUTPUT_PROGRAM_ACTIVE, HIGH);
  ++loopCount;

  // Write the loop count.
  printLabeledUInt32(0, 0, "L: ", loopCount, true, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles actions at the loop end.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleLoopEnd(uint16_t loopCompletedDelayMs) {
  digitalWrite(PIN_OUTPUT_PROGRAM_ACTIVE, LOW);
  delay(loopCompletedDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the interaction with the board's main thermistor. Returns temperature in Celcius.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float handleThermistor(uint8_t writeAdc, uint8_t writeResistance, uint16_t lcdPageCycleDelayMs) {
  float tempAverage = 0;
  unsigned long tempAccumulatedSamples = 0;

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Use VCC to measure against. Voltage readings will use the internal bandgap.
  // We can check if the default reference is already set in the ADMUX register:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // 0b11000000: Internal
  // 0b01000000: AVcc
  // 0b00000000: AREF
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if ((ADMUX & ((1 << REFS1) | (1 << REFS0))) != (1 << REFS0)) {
    analogReference(DEFAULT);

    // Dummy reads.
    for (uint8_t i = 0; i < 5; i++) {
      analogRead(PIN_INPUT_THERMISTOR_SENSE);
      delay(5);
    }
  }

  tempAccumulatedSamples = 0;
  for (uint8_t i = 0; i < THERMISTOR_READ_SAMPLES; i++) {
    tempAccumulatedSamples += analogRead(PIN_INPUT_THERMISTOR_SENSE);
    delay(5);
  }

  tempAverage = tempAccumulatedSamples / (float)THERMISTOR_READ_SAMPLES;

  // Write the ADC average.
  if (writeAdc) {
    printLabeledFloat(0, 0, "A: ", tempAverage, true, lcdPageCycleDelayMs);
  }

  // Calculate NTC resistance.
  tempAverage = 1023 / tempAverage - 1;
  tempAverage = THERMISTOR_REFERENCE / tempAverage;

  // Write the calculated resistance.
  if (writeResistance) {
    printLabeledFloat(0, 0, "R: ", tempAverage, true, lcdPageCycleDelayMs);
  }

  float tempNow = tempAverage / THERMISTOR_NOMINAL_RESISTANCE;           // (R / Ro)
  tempNow = log(tempNow);                                                // ln(R / Ro)
  tempNow /= THERMISTOR_BETA;                                            // 1 / B * ln(R / Ro)
  tempNow += 1.0 / (THERMISTOR_NOMINAL_TEMPERATURE + TEMP_ZERO_KELVIN);  // + (1 / To)
  tempNow = 1.0 / tempNow;                                               // Invert
  tempNow -= TEMP_ZERO_KELVIN;                                           // Convert absolute temperature to Celcius.

  if (tempNow < tempLifetimeExtrema.min) {
    tempLifetimeExtrema.min = tempNow;
  }
  if (tempNow > tempLifetimeExtrema.max) {
    tempLifetimeExtrema.max = tempNow;
  }

  // Write the calculated temperature and the min/max noticed.
  printLabeledFloat(0, 0, "T: ", tempNow, true, lcdPageCycleDelayMs);
  printLabeledFloat(0, 0, "Tmin: ", tempLifetimeExtrema.min, true, 0);
  printLabeledFloat(0, 1, "Tmax: ", tempLifetimeExtrema.max, false, lcdPageCycleDelayMs);

  return tempNow;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Displays program uptime.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleUptime(int16_t lcdPageCycleDelayMs) {
  static uint32_t lastMs = 0;
  static uint64_t totalSeconds = 0;

  uint32_t currentMs = millis();

  if (currentMs < lastMs) {
    totalSeconds += (UINT32_MAX - lastMs + currentMs) / 1000;
  } else {
    totalSeconds += (currentMs - lastMs) / 1000;
  }

  lastMs = currentMs;

  uint32_t days = totalSeconds / 86400;
  uint32_t hours = (totalSeconds % 86400) / 3600;
  uint32_t minutes = (totalSeconds % 3600) / 60;
  uint32_t seconds = totalSeconds % 60;

  printUptime(days, hours, minutes, seconds, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reads a breadboard rail voltage.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float readVoltage(uint8_t pin) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Use the internal bandgap to measure against. Prior temperature readings will use VCC. We can check if the internal
  // reference is already set in the ADMUX register:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // 0b11000000: Internal
  // 0b01000000: AVcc
  // 0b00000000: AREF
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if ((ADMUX & ((1 << REFS1) | (1 << REFS0))) != (1 << REFS1)) {
    analogReference(INTERNAL);

    // Dummy reads.
    for (uint8_t i = 0; i < 5; i++) {
      analogRead(pin);
      delay(5);
    }
  }

  unsigned long voltageAccumulatedSamples = 0;

  for (uint8_t i = 0; i < VOLTAGE_READ_SAMPLES; i++) {
    voltageAccumulatedSamples += analogRead(pin);
  }

  float adcAveraged = voltageAccumulatedSamples / (float)VOLTAGE_READ_SAMPLES;

  float pinVoltage = adcAveraged * VOLTAGE_BANDGAP_REFERENCE / 1023.0;
  float railVoltage = pinVoltage * (VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2) / VOLTAGE_DIVIDER_R2;

  return railVoltage;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reads the MCU's internal VCC voltage.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float readMcuVoltage(void) {
  unsigned long total = 0;

  for (uint8_t i = 0; i < VOLTAGE_READ_SAMPLES; i++) {
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
    delay(5);

    ADCSRA |= _BV(ADSC);
    while (bit_is_set(ADCSRA, ADSC))
      ;

    total += ADC;
  }

  float adcAveraged = total / (float)VOLTAGE_READ_SAMPLES;

  return (VOLTAGE_BANDGAP_REFERENCE * 1023.0) / adcAveraged;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Retrieves and displays voltages for the MCU and breadboard rails.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleVoltageReadings(int16_t lcdPageCycleDelayMs) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Capture voltages, extremas, display, and warn:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  handleVoltageReading(readMcuVoltage(),
                       "Vmcu",
                       &voltageMcuExtrema,
                       &thresholdsMcu,
                       &voltageMcuThresholdCounts,
                       lcdPageCycleDelayMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  handleVoltageReading(readVoltage(PIN_INPUT_VCC_SENSE),
                       "Vcc",
                       &voltageVccExtrema,
                       &thresholdsVcc,
                       &voltageVccThresholdCounts,
                       lcdPageCycleDelayMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  handleVoltageReading(readVoltage(PIN_INPUT_5V_SENSE),
                       "5V",
                       &voltage5VExtrema,
                       &thresholds5V,
                       &voltage5VThresholdCounts,
                       lcdPageCycleDelayMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  handleVoltageReading(readVoltage(PIN_INPUT_3V3_SENSE),
                       "3V3",
                       &voltage3V3Extrema,
                       &thresholds3V3,
                       &voltage3V3ThresholdCounts,
                       lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles rails atomically.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleVoltageReading(float voltage, const char* const rail, F_EXTREMA_T* const extrema, const F_EXTREMA_T* const thresholds, UI32_EXTREMA_T* const thresholdExceededCounts, const int16_t lcdPageCycleDelayMs) {
  char label[16];

  // Active voltage:
  captureExtrema(voltage, extrema);
  snprintf(label, sizeof(label), "%s: ", rail);
  printLabeledFloat(0, 0, label, voltage, true, lcdPageCycleDelayMs);

  // Low/high warnings:
  checkThresholds(rail, voltage, thresholds, thresholdExceededCounts, lcdPageCycleDelayMs);

  // Lifetime min/max's:
  snprintf(label, sizeof(label), "%s Min: ", rail);
  printLabeledFloat(0, 0, label, extrema->min, true, 0);
  snprintf(label, sizeof(label), "%s Max: ", rail);
  printLabeledFloat(0, 1, label, extrema->max, false, lcdPageCycleDelayMs);

  // Lifetime thresholds met or exceeded counts:
  snprintf(label, sizeof(label), "%s Vlc: ", rail);
  printLabeledUInt32(0, 0, label, thresholdExceededCounts->min, true, 0);
  snprintf(label, sizeof(label), "%s Vhc: ", rail);
  printLabeledUInt32(0, 1, label, thresholdExceededCounts->max, false, lcdPageCycleDelayMs);

  // Shutdown if the voltage low/high counts exceed the maximum allowed.
  if (thresholdExceededCounts->min >= VOLTAGE_LOW_MAXIMUM_CYCLE_COUNT) {
    shutdown(ERROR_CODE_VOLTAGE_LOW_COUNT, NULL);
  } else if (thresholdExceededCounts->max >= VOLTAGE_HIGH_MAXIMUM_CYCLE_COUNT) {
    shutdown(ERROR_CODE_VOLTAGE_HIGH_COUNT, NULL);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Updates the extremas for the measured voltages.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void captureExtrema(float voltage, F_EXTREMA_T* const extrema) {
  if (voltage < extrema->min) {
    extrema->min = voltage;
  }
  if (voltage > extrema->max) {
    extrema->max = voltage;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks the provided voltage against the allowed thresholds to determine if a warning should be raised.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void checkThresholds(const char* const rail, float voltage, const F_EXTREMA_T* const thresholds, UI32_EXTREMA_T* const counts, uint16_t lcdPageCycleDelayMs) {
  if (voltage <= thresholds->min) {
    counts->min++;
    warn(rail, "Low", voltage, thresholds, lcdPageCycleDelayMs, &melodyVoltageThresholds);
  } else if (voltage >= thresholds->max) {
    counts->max++;
    warn(rail, "High", voltage, thresholds, lcdPageCycleDelayMs, &melodyVoltageThresholds);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Display and warn for exceeded voltage thresholds.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void warn(const char* const rail, const char* const state, float voltage, const F_EXTREMA_T* const thresholds, uint16_t lcdPageCycleDelayMs, const MELODY_T* const melody) {
  char warn[16], range[16], floatMin[8], floatMax[8];

  snprintf(warn, sizeof(warn), "%s %s: ", rail, state);

  dtostrf(thresholds->min, 1, 2, floatMin);
  dtostrf(thresholds->max, 1, 2, floatMax);
  snprintf(range, sizeof(range), "(%s - %s)", floatMin, floatMax);

  playMelody(melody);
  printLabeledFloat(0, 0, warn, voltage, true, 0);
  printString(0, 1, range, false, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tracks the min and max cooldown durations.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void captureCooldownDuration(uint32_t cooldownStartMs) {
  uint32_t deltaMs = millis() - cooldownStartMs;

  if (deltaMs > 0 && deltaMs < cooldownDurationLifetimeMsExtrema.min) {
    cooldownDurationLifetimeMsExtrema.min = deltaMs;
  }
  if (deltaMs > cooldownDurationLifetimeMsExtrema.max) {
    cooldownDurationLifetimeMsExtrema.max = deltaMs;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles self-termination if the cooldown exceeds the max duration.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t handleCooldownExceeded(uint32_t cooldownStartMs, uint8_t cooldownMinutesMax) {
  uint32_t elapsedMs = millis() - cooldownStartMs;

  uint32_t maxCooldownMs = (uint32_t)cooldownMinutesMax * 60UL * 1000UL;

  return (elapsedMs >= maxCooldownMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Displays the formatted min and max cooldown durations.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleCooldownMessage(const UI32_EXTREMA_T* const cooldownDurationLifetimeMsExtrema, uint16_t lcdPageCycleDelayMs) {
  char minDuration[16], maxDuration[16];

  if (cooldownDurationLifetimeMsExtrema->min == UINT32_MAX) {
    snprintf(minDuration, sizeof(minDuration), "Dmin: -- m -- s");
  } else {
    uint32_t minSeconds = cooldownDurationLifetimeMsExtrema->min / 1000;
    uint32_t minMinutes = minSeconds / 60;
    minSeconds = minSeconds % 60;
    snprintf(minDuration, sizeof(minDuration), "Dmin: %02" PRIu32 " m %02" PRIu32 " s", minMinutes, minSeconds);
  }

  if (cooldownDurationLifetimeMsExtrema->max == 0) {
    snprintf(maxDuration, sizeof(maxDuration), "Dmax: -- m -- s");
  } else {
    uint32_t maxSeconds = cooldownDurationLifetimeMsExtrema->max / 1000;
    uint32_t maxMinutes = maxSeconds / 60;
    maxSeconds = maxSeconds % 60;
    snprintf(maxDuration, sizeof(maxDuration), "Dmax: %02" PRIu32 " m %02" PRIu32 " s", maxMinutes, maxSeconds);
  }

  printString(0, 0, minDuration, true, 0);
  printString(0, 1, maxDuration, false, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the board's fan considering the current temperature, temperature bounds, and cooldown duration.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleThermistorFan(float currentTemperature, float tempLowerbound, float tempUpperbound, float tempMax, uint8_t cooldownMinutesMax, uint16_t lcdPageCycleDelayMs) {
  static uint32_t cooldownStartMs = 0;
  static uint8_t isCoolingDown = false;

  if (currentTemperature >= tempUpperbound) {
    // Warn that it's above max.
    if (currentTemperature >= tempMax) {
      playMelody(&melodyMaxTemp);
      // Turn power off if we've been consistently running hot.
      if (tempHighCycleCount++ >= TEMP_MAXIMUM_CYCLE_COUNT) {
        shutdown(ERROR_CODE_HOT_CYCLES, NULL);
      }
    } else {
      // Warn that it's above upperbound.
      playMelody(&melodyFanOn);
    }

    if (!isCoolingDown) {
      isCoolingDown = true;

      digitalWrite(PIN_OUTPUT_FAN_ENABLE, HIGH);
      cooldownStartMs = millis();

      ++fanCycleCount;
      printString(0, 0, "Fan Enabled", true, lcdPageCycleDelayMs);
    }
  } else if (currentTemperature <= tempLowerbound && isCoolingDown) {
    playMelody(&melodyFanOff);

    isCoolingDown = false;
    digitalWrite(PIN_OUTPUT_FAN_ENABLE, LOW);
    captureCooldownDuration(cooldownStartMs);

    printString(0, 0, "Fan Disabled", true, lcdPageCycleDelayMs);
  }

  // Handle if we're not cooling down fast enough.
  if (isCoolingDown && handleCooldownExceeded(cooldownStartMs, cooldownMinutesMax)) {
    shutdown(ERROR_CODE_COOLDOWN, NULL);
  }

  // Fan temperature bounds.
  printLabeledFloat(0, 0, "Fmin: ", tempLowerbound, true, 0);
  printLabeledFloat(0, 1, "Fmax: ", tempUpperbound, false, lcdPageCycleDelayMs);

  // Write out min and max cooldown durations.
  handleCooldownMessage(&cooldownDurationLifetimeMsExtrema, lcdPageCycleDelayMs);

  // Number of cycles the fan was turned on.
  printLabeledUInt16(0, 0, "Fcc: ", fanCycleCount, true, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////