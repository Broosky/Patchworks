/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Project: Patchworks                                                                                                     //
// Author: Jeffrey Bednar                                                                                                  //
// Copyright (c) Illusion Interactive, 2011 - 2026.                                                                        //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef _LCD_H_
#define _LCD_H_
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "common.h"
#include "fixed_point.h"
#include <LiquidCrystal.h>
#include <LiquidCrystal_I2C.h>
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function prototypes:
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void            testLcd                   (uint8_t, uint16_t, uint8_t);
void            printLabeledFixed         (uint8_t, uint8_t, const char* const, fixed16_t, bool, uint16_t, uint8_t);
void            printLabeledFloat         (uint8_t, uint8_t, const char* const, float, bool, uint16_t);
void            printLabeledUInt8         (uint8_t, uint8_t, const char* const, uint8_t, bool, uint16_t);
void            printLabeledUInt16        (uint8_t, uint8_t, const char* const, uint16_t, bool, uint16_t);
void            printLabeledUInt32        (uint8_t, uint8_t, const char* const, uint32_t, bool, uint16_t);
void            printLabeledInt32         (uint8_t, uint8_t, const char* const, int32_t, bool, uint16_t);
void            printLabeledString        (uint8_t, uint8_t, const char* const, const char* const, bool, uint16_t);
void            printString               (uint8_t, uint8_t, const char* const, bool, uint16_t);
void            printFloat                (uint8_t, uint8_t, float, bool, uint16_t);
void            printUptime               (uint32_t, uint32_t, uint32_t, uint32_t, uint16_t);
void            printOut                  (uint8_t, uint8_t, const char* const, bool, uint16_t);
void            write                     (uint8_t, uint8_t, const char* const, bool);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////