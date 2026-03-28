/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Project: Patchworks                                                                                                     //
// Author: Jeffrey Bednar                                                                                                  //
// Copyright (c) Illusion Interactive, 2011 - 2026.                                                                        //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "Headers/lcd.h"
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static const uint8_t LCD_MAX_X = 16;
static const uint8_t LCD_MAX_Y = 2;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 16, 2);  // SDA -> A4, SCL -> A5
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
void printLabeledFixed(uint8_t x, uint8_t y, const char* const label, fixed16_t value, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs, uint8_t shift) {
  char buffer[16];

  fixed16_t fixedOne = 1 << shift;

  int32_t component = value >> shift;
  int32_t fractionalRaw = value & (fixedOne - 1);  // Subtract for 0xFF... to mask out fractional.

  // Convert fraction to 3 decimal digits (multiply by 1000 then shift).
  int32_t fractionalPart = (fractionalRaw * 1000) >> shift;

  // Keep fractional part positive.
  if (value < 0) {
    fractionalPart = abs(fractionalPart);
  }

  snprintf(buffer, sizeof(buffer), "%s%ld.%02ld", label, (long)component, (long)fractionalPart);
  printOut(x, y, buffer, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printFloat(uint8_t x, uint8_t y, float value, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  char floatString[16];

  // Same precision as fixed print.
  dtostrf(value, 1, 2, floatString);
  printOut(x, y, floatString, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printLabeledFloat(uint8_t x, uint8_t y, const char* const label, float value, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  char buffer[16], floatString[16];

  // Same precision as fixed print.
  dtostrf(value, 1, 2, floatString);
  snprintf(buffer, sizeof(buffer), "%s%s", label, floatString);
  printOut(x, y, buffer, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printLabeledUInt8(uint8_t x, uint8_t y, const char* const label, uint8_t value, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  char buffer[16];

  snprintf(buffer, sizeof(buffer), "%s%" PRIu8, label, value);
  printOut(x, y, buffer, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printLabeledUInt16(uint8_t x, uint8_t y, const char* const label, uint16_t value, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  char buffer[16];

  snprintf(buffer, sizeof(buffer), "%s%" PRIu16, label, value);
  printOut(x, y, buffer, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printLabeledUInt32(uint8_t x, uint8_t y, const char* const label, uint32_t value, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  char buffer[16];

  snprintf(buffer, sizeof(buffer), "%s%" PRIu32, label, value);
  printOut(x, y, buffer, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printLabeledInt32(uint8_t x, uint8_t y, const char* const label, int32_t value, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  char buffer[16];

  snprintf(buffer, sizeof(buffer), "%s%" PRId32, label, value);
  printOut(x, y, buffer, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printLabeledString(uint8_t x, uint8_t y, const char* const label, const char* const text, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  char buffer[16];

  snprintf(buffer, sizeof(buffer), "%s%s", label, text);
  printOut(x, y, buffer, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printString(uint8_t x, uint8_t y, const char* const text, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  printOut(x, y, text, clearBeforeWrite, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printUptime(uint32_t days, uint32_t hours, uint32_t minutes, uint32_t seconds, uint16_t lcdPageCycleDelayMs) {
  char buffer[16];

  snprintf(buffer, sizeof(buffer), "Up: %02" PRIu32 " d %02" PRIu32 " h", days, hours);
  printString(0, 0, buffer, true, 0);

  snprintf(buffer, sizeof(buffer), "Up: %02" PRIu32 " m %02" PRIu32 " s", minutes, seconds);
  printString(0, 1, buffer, false, lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void printOut(uint8_t x, uint8_t y, const char* const text, bool clearBeforeWrite, uint16_t lcdPageCycleDelayMs) {
  write(x, y, text, clearBeforeWrite);
  delay(lcdPageCycleDelayMs);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void write(uint8_t x, uint8_t y, const char* const text, bool clearBeforeWrite) {
  if (x < LCD_MAX_X && y < LCD_MAX_Y) {
    if (clearBeforeWrite) {
      lcd.clear();
    }

    lcd.setCursor(x, y);
    lcd.print(text);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////