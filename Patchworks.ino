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
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "Headers/fixed_point.h"
#include "Headers/lcd.h"
#include <EEPROM.h>  // 1024 bytes available, addresses: 0 - 1023, width: 8 bits. Degrades.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern LiquidCrystal_I2C lcd;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Firmware version:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t FW_VERSION_MAJOR = 1;
const uint8_t FW_VERSION_MINOR = 3;
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
} I_EXTREMA_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct UI32_EXTREMA {
  uint32_t min;
  uint32_t max;
} UI32_EXTREMA_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct MELODY {
  uint32_t frequency;
  int32_t durationMs;
  uint8_t cycles;
  uint16_t toneCycleDelayMs;
  uint8_t pauseBeforeReturn;
} MELODY_T;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program constants:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t PIN_OUTPUT_BUZZER = 8;
const uint8_t PIN_OUTPUT_FAN_ENABLE = 9;
const uint8_t PIN_OUTPUT_RELAY_ENABLE = 6;
const uint8_t PIN_OUTPUT_PROGRAM_ACTIVE = 2;
const uint8_t PIN_OUTPUT_EXTERNAL_CLOCK = 10;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t PIN_INPUT_BB_VCC_SENSE = A1;
const uint8_t PIN_INPUT_BB_5V_SENSE = A2;
const uint8_t PIN_INPUT_BB_3V3_SENSE = A3;
const uint8_t PIN_INPUT_THERMISTOR_SENSE = A0;
const uint8_t PIN_INPUT_BREADBOARD_ERROR_SENSE = 11;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const uint8_t VOLTAGE_READ_SAMPLES = 16;
const float VOLTAGE_BANDGAP_REFERENCE = 1.1;  // Should be measured per MCU as this varies.
const float VOLTAGE_DIVIDER_R1 = 47000.0;     // Common across VCC, 5V, and 3V3 voltage rails (1%). Allows safe input readings
                                              // for comparison with the bandgap if VCC reaches up to 24V. Although the
                                              // Arduino regulator supports up to 12V, this provides additional survivability
                                              // in the event if the main VCC regulator fails or there's a rail short. Regardless, the
                                              // hardware overvoltage protection will trigger at approximately 12.7V, 5.8V,
                                              // and 4.0V for each respective rail.
const float VOLTAGE_DIVIDER_R2 = 2000.0;
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
const int32_t EXTERNAL_CLOCK_LOWERBOUND = 0;
const int32_t EXTERNAL_CLOCK_UPPERBOUND = 99;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* ERROR_CODE_NAMES[] = {
  "None",  // Align with ERROR_CODE.
  "Invalid",
  "Hot Cycles",
  "Cooldown",
  "Breadboard",
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const F_EXTREMA vMcuThresholds = { .min = 4.5, .max = 5.5 };
const F_EXTREMA vBbVccThresholds = { .min = 11.5, .max = 12.5 };
const F_EXTREMA vBb5VThresholds = { .min = 4.5, .max = 5.5 };
const F_EXTREMA vBb3V3Thresholds = { .min = 3.0, .max = 3.5 };
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const MELODY_T buzzerTest = {
  .frequency = 1000,
  .durationMs = 50,
  .cycles = 3,
  .toneCycleDelayMs = 100,
  .pauseBeforeReturn = true
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const MELODY_T buzzerFanOn = {
  .frequency = 2000,
  .durationMs = 50,
  .cycles = 3,
  .toneCycleDelayMs = 100,
  .pauseBeforeReturn = true
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const MELODY_T buzzerFanOff = {
  .frequency = 2000,
  .durationMs = 50,
  .cycles = 1,
  .toneCycleDelayMs = 100,
  .pauseBeforeReturn = true
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const MELODY_T buzzerVoltageThresholds = {
  .frequency = 1750,
  .durationMs = 50,
  .cycles = 3,
  .toneCycleDelayMs = 100,
  .pauseBeforeReturn = true
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const MELODY_T buzzerMaxTemp = {
  .frequency = 3000,
  .durationMs = 50,
  .cycles = 3,
  .toneCycleDelayMs = 100,
  .pauseBeforeReturn = true
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const MELODY_T buzzerShutdown = {
  .frequency = 4000,
  .durationMs = 1000,
  .cycles = 15,
  .toneCycleDelayMs = 250,
  .pauseBeforeReturn = true
};
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Program globals and counters:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint16_t fanCycleCount = 0;                                // Cumulative, never resets.
uint8_t tempHighCycleCount = 0;                            // Cumulative, never resets.
F_EXTREMA_T voltageMcu = { .min = 999.9, .max = -999.9 };  // Inverted to normalize during runtime.
F_EXTREMA_T voltageBbVcc = { .min = 999.9, .max = -999.9 };
F_EXTREMA_T voltageBb5V = { .min = 999.9, .max = -999.9 };
F_EXTREMA_T voltageBb3V3 = { .min = 999.9, .max = -999.9 };
F_EXTREMA_T tempLifetime = { .min = 999.9, .max = -999.9 };
I_EXTREMA_T externalClockLifetime = { .min = EXTERNAL_CLOCK_UPPERBOUND << 1, .max = -(EXTERNAL_CLOCK_UPPERBOUND << 1) };
UI32_EXTREMA_T cooldownDurationLifetimeMs = { .min = UINT32_MAX, .max = 0 };
uint8_t showSplashScreen = true;
uint8_t showLastKnownError = true;
uint32_t loopCount = 0;
uint8_t allPixels[8] = {
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
};
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
  pinMode(PIN_INPUT_BB_VCC_SENSE, INPUT);
  pinMode(PIN_INPUT_BB_5V_SENSE, INPUT);
  pinMode(PIN_INPUT_BB_3V3_SENSE, INPUT);
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
  testBuzzer(buzzerTest);
  testThermistorFan(3, 500);
  setPowerOnLatch(HIGH);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main program loop.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop(void) {
  handleSplashScreen(DELAY_LCD_PAGE_CYCLE);
  handleLastKnownError(DELAY_LCD_PAGE_CYCLE, buzzerShutdown);
  handleLoopStart(DELAY_LCD_PAGE_CYCLE);
  handleBreadboardError(buzzerShutdown);

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

  handleVoltageReadings(DELAY_LCD_PAGE_CYCLE,
                        buzzerVoltageThresholds);

  handleUptime(DELAY_LCD_PAGE_CYCLE);

  handleLoopEnd(DELAY_LOOP_COMPLETED);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Seed by mixing analog noise and runtime for a higher degree of entropy.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setupSeed() {
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
// Check and warn if Snubby or breadboard circuitry is reporting an error.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleBreadboardError(MELODY_T melody) {
  uint8_t breadboardError = digitalRead(PIN_INPUT_BREADBOARD_ERROR_SENSE);

  if (!breadboardError) {
    shutdown(ERROR_CODE_BREADBOARD, NULL, melody);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* getErrorCodeName(ERROR_CODE_T errorCode) {
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

  if (randomNumber < externalClockLifetime.min) {
    externalClockLifetime.min = randomNumber;
  }
  if (randomNumber > externalClockLifetime.max) {
    externalClockLifetime.max = randomNumber;
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
  printLabeledInt(0, 0, "C: ", randomNumber, true, lcdPageCycleDelayMs);
  printLabeledInt(0, 0, "Cmin: ", externalClockLifetime.min, true, 0);
  printLabeledInt(0, 1, "Cmax: ", externalClockLifetime.max, false, lcdPageCycleDelayMs);
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
void testBuzzer(MELODY_T melody) {
  playMelody(melody);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tests the board LCD.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void testLcd(uint8_t backlightCycles, uint16_t backlightCycleDelayMs, uint8_t pixelCycleDelayMs) {
  for (uint8_t i = 0; i < backlightCycles; i++) {
    lcd.backlight();
    delay(backlightCycleDelayMs);
    if (i < backlightCycles - 1) {
      lcd.noBacklight();
      delay(backlightCycleDelayMs);
    }
  }

  lcd.cursor_on();
  lcd.blink_on();

  lcd.setBacklight(255);
  lcd.setContrast(255);

  lcd.createChar(0, allPixels);

  // Test pixels.
  for (uint8_t i = 0; i < LCD_MAX_Y; i++) {
    for (uint8_t j = 0; j < LCD_MAX_X; j++) {
      lcd.setCursor(j, i);
      lcd.write(byte(0));
      delay(pixelCycleDelayMs);
    }
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plays a tone on the board's buzzer.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void playMelody(MELODY_T melody) {
  for (uint8_t i = 0; i < melody.cycles; i++) {
    tone(PIN_OUTPUT_BUZZER, melody.frequency, melody.durationMs);
    if (i < melody.cycles - 1) {
      // Wait until the tone finishes.
      delay(melody.durationMs);
      delay(melody.toneCycleDelayMs);
    }
  }

  // Avoid buzzer chirp during other transistions.
  if (melody.pauseBeforeReturn) {
    delay(100);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main error handler.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void shutdown(ERROR_CODE_T errorCode, uint32_t* errorFlags, MELODY_T melody) {
  // Playing the tone acts as the LCD page delay.
  printLabeledInt(0, 0, "E: ", errorCode, true, 0);
  printLabeledString(0, 1, "E: ", getErrorCodeName(errorCode), false, 0);

  size_t address = 0;
  EEPROM.put(address, errorCode);
  address += sizeof(errorCode);

  if (errorFlags) {
    EEPROM.put(address, *errorFlags);
    address += sizeof(*errorFlags);
  }

  playMelody(melody);
  delay(5000);

  // Self-terminate.
  setPowerOnLatch(LOW);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Displays a splash screen on the LCD.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleSplashScreen(uint16_t lcdPageCycleDelayMs) {
  if (showSplashScreen) {
    char version[32], date[32], time[32];

    printString(0, 0, "Illusion", true, 0);
    printString(0, 1, "Interactive", false, lcdPageCycleDelayMs);

    sprintf(version, "v%u.%u.%u", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);

    printString(0, 0, "Patchworks", true, 0);
    printString(0, 1, version, false, lcdPageCycleDelayMs);

    sprintf(date, "%s", __DATE__);
    sprintf(time, "%s", __TIME__);

    printString(0, 0, date, true, 0);
    printString(0, 1, time, false, lcdPageCycleDelayMs);

    showSplashScreen = false;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles the last known error on startup if there was an automatic fault shutdown.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleLastKnownError(uint16_t lcdPageCycleDelayMs, MELODY_T melody) {
  if (showLastKnownError) {
    ERROR_CODE_T errorCode;
    uint32_t errorFlags;
    size_t address = 0;

    EEPROM.get(address, errorCode);
    address += sizeof(errorCode);

    if (errorCode > ERROR_CODE_NONE) {
      EEPROM.get(address, errorFlags);
      address += sizeof(errorFlags);

      printLabeledInt(0, 0, "El: ", errorCode, true, 0);
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
  printLabeledInt(0, 0, "L: ", loopCount, true, lcdPageCycleDelayMs);
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

  if (tempNow < tempLifetime.min) {
    tempLifetime.min = tempNow;
  }
  if (tempNow > tempLifetime.max) {
    tempLifetime.max = tempNow;
  }

  // Write the calculated temperature and the min/max noticed.
  printLabeledFloat(0, 0, "T: ", tempNow, true, lcdPageCycleDelayMs);
  printLabeledFloat(0, 0, "Tmin: ", tempLifetime.min, true, 0);
  printLabeledFloat(0, 1, "Tmax: ", tempLifetime.max, false, lcdPageCycleDelayMs);

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
void handleVoltageReadings(int16_t lcdPageCycleDelayMs, MELODY_T warningMelody) {
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Capture voltages, extremas, display, and warn:
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  float mcuVcc = readMcuVoltage();
  captureExtrema(mcuVcc, &voltageMcu);
  printLabeledFloat(0, 0, "Vmcu: ", mcuVcc, true, lcdPageCycleDelayMs);
  checkThresholds("Vmcu", mcuVcc, vMcuThresholds, lcdPageCycleDelayMs, warningMelody);
  printLabeledFloat(0, 0, "Vmcu Min: ", voltageMcu.min, true, 0);
  printLabeledFloat(0, 1, "Vmcu Max: ", voltageMcu.max, false, lcdPageCycleDelayMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  float bbVcc = readVoltage(PIN_INPUT_BB_VCC_SENSE);
  captureExtrema(bbVcc, &voltageBbVcc);
  printLabeledFloat(0, 0, "Vcc: ", bbVcc, true, lcdPageCycleDelayMs);
  checkThresholds("Vcc", bbVcc, vBbVccThresholds, lcdPageCycleDelayMs, warningMelody);
  printLabeledFloat(0, 0, "Vcc Min: ", voltageBbVcc.min, true, 0);
  printLabeledFloat(0, 1, "Vcc Max: ", voltageBbVcc.max, false, lcdPageCycleDelayMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  float bb5V = readVoltage(PIN_INPUT_BB_5V_SENSE);
  captureExtrema(bb5V, &voltageBb5V);
  printLabeledFloat(0, 0, "5V: ", bb5V, true, lcdPageCycleDelayMs);
  checkThresholds("5V", bb5V, vBb5VThresholds, lcdPageCycleDelayMs, warningMelody);
  printLabeledFloat(0, 0, "5V Min: ", voltageBb5V.min, true, 0);
  printLabeledFloat(0, 1, "5V Max: ", voltageBb5V.max, false, lcdPageCycleDelayMs);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  float bb3V3 = readVoltage(PIN_INPUT_BB_3V3_SENSE);
  captureExtrema(bb3V3, &voltageBb3V3);
  printLabeledFloat(0, 0, "3V3: ", bb3V3, true, lcdPageCycleDelayMs);
  checkThresholds("3V3", bb3V3, vBb3V3Thresholds, lcdPageCycleDelayMs, warningMelody);
  printLabeledFloat(0, 0, "3V3 Min: ", voltageBb3V3.min, true, 0);
  printLabeledFloat(0, 1, "3V3 Max: ", voltageBb3V3.max, false, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Updates the extremas for the measured voltages.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void captureExtrema(float voltage, F_EXTREMA_T* extrema) {
  if (voltage < extrema->min) {
    extrema->min = voltage;
  }
  if (voltage > extrema->max) {
    extrema->max = voltage;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checks the provided voltage against the allowed thresholds to determine if a warning should sound.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void checkThresholds(const char* rail, float voltage, F_EXTREMA_T thresholds, uint16_t lcdPageCycleDelayMs, MELODY_T melody) {
  if (voltage <= thresholds.min) {
    warn(rail, "Low", voltage, thresholds, lcdPageCycleDelayMs, melody);
  } else if (voltage >= thresholds.max) {
    warn(rail, "High", voltage, thresholds, lcdPageCycleDelayMs, melody);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Display and warn for exceeded voltage thresholds.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void warn(const char* rail, const char* state, float voltage, F_EXTREMA_T thresholds, uint16_t lcdPageCycleDelayMs, MELODY_T melody) {
  char warn[32], range[32], floatMin[8], floatMax[8];

  snprintf(warn, sizeof(warn), "%s %s: ", rail, state);

  dtostrf(thresholds.min, 1, 2, floatMin);
  dtostrf(thresholds.max, 1, 2, floatMax);
  snprintf(range, sizeof(range), "(%s - %s)", floatMin, floatMax);

  playMelody(melody);
  printLabeledFloat(0, 0, warn, voltage, true, 0);
  printString(0, 1, range, false, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tracks the min and max cooldown durations.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void captureCooldownDuration(uint32_t cooldownStartMs) {
  uint32_t currentMs = millis();
  uint32_t deltaMs = currentMs - cooldownStartMs;

  if (deltaMs > 0 && deltaMs < cooldownDurationLifetimeMs.min) {
    cooldownDurationLifetimeMs.min = deltaMs;
  }
  if (deltaMs > cooldownDurationLifetimeMs.max) {
    cooldownDurationLifetimeMs.max = deltaMs;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles self-termination if the cooldown exceeds the max duration.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t handleCooldownExceeded(uint32_t cooldownStartMs, uint8_t cooldownMinutesMax) {
  uint32_t currentMs = millis();
  uint32_t elapsedMs = currentMs - cooldownStartMs;

  uint32_t maxCooldownMs = (uint32_t)cooldownMinutesMax * 60UL * 1000UL;

  return (elapsedMs >= maxCooldownMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Displays the formatted min and max cooldown durations.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void handleCooldownMessage(UI32_EXTREMA_T cooldownDurationLifetimeMsExtrema, uint16_t lcdPageCycleDelayMs) {
  char minDuration[32], maxDuration[32];

  if (cooldownDurationLifetimeMsExtrema.min == UINT32_MAX) {
    sprintf(minDuration, "Dmin: --m --s");
  } else {
    uint32_t minSeconds = cooldownDurationLifetimeMsExtrema.min / 1000;
    uint32_t minMinutes = minSeconds / 60;
    minSeconds = minSeconds % 60;
    sprintf(minDuration, "Dmin: %02lum %02lus", minMinutes, minSeconds);
  }

  if (cooldownDurationLifetimeMsExtrema.max == 0) {
    sprintf(maxDuration, "Dmax: --m --s");
  } else {
    uint32_t maxSeconds = cooldownDurationLifetimeMsExtrema.max / 1000;
    uint32_t maxMinutes = maxSeconds / 60;
    maxSeconds = maxSeconds % 60;
    sprintf(maxDuration, "Dmax: %02lum %02lus", maxMinutes, maxSeconds);
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
      playMelody(buzzerMaxTemp);
      // Turn power off if we've been consistently running hot.
      if (tempHighCycleCount++ >= TEMP_MAXIMUM_CYCLE_COUNT) {
        shutdown(ERROR_CODE_HOT_CYCLES, NULL, buzzerShutdown);
      }
    } else {
      // Warn that it's above upperbound.
      playMelody(buzzerFanOn);
    }

    if (!isCoolingDown) {
      isCoolingDown = true;

      digitalWrite(PIN_OUTPUT_FAN_ENABLE, HIGH);
      cooldownStartMs = millis();

      ++fanCycleCount;
      printString(0, 0, "Fan Enabled", true, lcdPageCycleDelayMs);
    }
  } else if (currentTemperature <= tempLowerbound && isCoolingDown) {
    playMelody(buzzerFanOff);

    isCoolingDown = false;
    digitalWrite(PIN_OUTPUT_FAN_ENABLE, LOW);
    captureCooldownDuration(cooldownStartMs);

    printString(0, 0, "Fan Disabled", true, lcdPageCycleDelayMs);
  }

  // Handle if we're not cooling down fast enough.
  if (isCoolingDown && handleCooldownExceeded(cooldownStartMs, cooldownMinutesMax)) {
    shutdown(ERROR_CODE_COOLDOWN, NULL, buzzerShutdown);
  }

  // Fan temperature bounds.
  printLabeledFloat(0, 0, "Fmin: ", tempLowerbound, true, 0);
  printLabeledFloat(0, 1, "Fmax: ", tempUpperbound, false, lcdPageCycleDelayMs);

  // Write out min and max cooldown durations.
  handleCooldownMessage(cooldownDurationLifetimeMs, lcdPageCycleDelayMs);

  // Number of cycles the fan was turned on.
  printLabeledInt(0, 0, "Fcc: ", fanCycleCount, true, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////