#ifndef WIFIFORM_H_INCLUDE
#define WIFIFORM_H_INCLUDE
#include <Arduino.h>

extern const String WiFiFormPart1;
extern const String WiFiFormPart2;
extern const String autoCloseHtml;

String htmlEscapedString(String sourceString);

#endif