#pragma once

#include <Arduino.h>

void midiScannerBegin();
bool midiScannerIsConnected();
const String &midiScannerTarget();

bool midiScannerSendControlChange(
  uint8_t control,
  uint8_t value,
  uint8_t channel
);

void midiScannerSetCapture(bool enabled);
bool midiScannerCaptureEnabled();
void midiScannerClearCapture();
String midiScannerCaptureLog();

void midiScannerLog(const String &line);
void midiScannerClearEventLog();
String midiScannerEventLog();