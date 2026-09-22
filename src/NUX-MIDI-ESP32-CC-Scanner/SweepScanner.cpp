#include "SweepScanner.h"

#include "MidiScanner.h"

namespace
{
  const uint8_t MAX_VALUES = 64;
  const uint16_t MIN_INTERVAL_MS = 250;
  const uint16_t MAX_INTERVAL_MS = 60000;

  SweepConfig currentConfig = {
    7,
    1,
    3,
    500,
    "1,32,64,127"
  };

  uint8_t values[MAX_VALUES];
  uint8_t valueCount = 0;
  uint8_t valueIndex = 0;
  uint8_t currentChannel = 1;
  unsigned long nextSendAt = 0;
  SweepState state = SWEEP_STOPPED;

  bool parseValues(const String &text, String &error)
  {
    valueCount = 0;
    int start = 0;

    while (start < text.length()) {
      const int separator = text.indexOf(',', start);
      const int end = separator < 0 ? text.length() : separator;
      String token = text.substring(start, end);
      token.trim();

      if (token.length() == 0) {
        error = "Values contain an empty item";
        return false;
      }

      const int value = token.toInt();
      if (value < 0 || value > 127 || String(value) != token) {
        error = "Each value must be an integer from 0 to 127";
        return false;
      }

      if (valueCount >= MAX_VALUES) {
        error = "Too many values";
        return false;
      }

      values[valueCount++] = (uint8_t)value;
      if (separator < 0)
        break;
      start = separator + 1;
    }

    if (valueCount == 0) {
      error = "At least one value is required";
      return false;
    }

    return true;
  }

  void advanceAfterSend()
  {
    valueIndex++;
    if (valueIndex < valueCount)
      return;

    valueIndex = 0;
    currentChannel++;
    if (currentChannel <= currentConfig.lastChannel)
      return;

    state = SWEEP_STOPPED;
    midiScannerLog("CC sweep completed");
  }

  bool sameConfig(const SweepConfig &left, const SweepConfig &right)
  {
    return left.control == right.control &&
           left.firstChannel == right.firstChannel &&
           left.lastChannel == right.lastChannel &&
           left.intervalMs == right.intervalMs &&
           left.values == right.values;
  }
}

void sweepScannerBegin()
{
  values[0] = 1;
  values[1] = 32;
  values[2] = 64;
  values[3] = 127;
  valueCount = 4;
}

void sweepScannerLoop()
{
  if (state != SWEEP_RUNNING)
    return;

  if (millis() < nextSendAt)
    return;

  if (!midiScannerIsConnected()) {
    nextSendAt = millis() + 1000;
    midiScannerLog("CC sweep waiting for BLE-MIDI connection");
    return;
  }

  if (midiScannerSendControlChange(
        currentConfig.control,
        values[valueIndex],
        currentChannel
      )) {
    advanceAfterSend();
    nextSendAt = millis() + currentConfig.intervalMs;
  } else {
    nextSendAt = millis() + 1000;
  }
}

bool sweepScannerStart(const SweepConfig &config, String &error)
{
  if (state == SWEEP_PAUSED && sameConfig(config, currentConfig)) {
    state = SWEEP_RUNNING;
    nextSendAt = millis();
    midiScannerLog("CC sweep resumed");
    return true;
  }

  if (!sweepScannerControlAllowed(config.control)) {
    error = sweepScannerBlockedReason(config.control);
    return false;
  }

  if (config.firstChannel < 1 ||
      config.firstChannel > 16 ||
      config.lastChannel < config.firstChannel ||
      config.lastChannel > 16) {
    error = "Channel range must be from 1 to 16";
    return false;
  }

  if (config.intervalMs < MIN_INTERVAL_MS ||
      config.intervalMs > MAX_INTERVAL_MS) {
    error = "Interval must be from 250 to 60000 ms";
    return false;
  }

  if (!parseValues(config.values, error))
    return false;

  currentConfig = config;
  currentChannel = config.firstChannel;
  valueIndex = 0;
  nextSendAt = 0;
  state = SWEEP_RUNNING;

  midiScannerLog(
    "CC sweep started: control=" + String(config.control) +
    " channels=" + String(config.firstChannel) + "-" +
    String(config.lastChannel) +
    " values=" + config.values +
    " interval=" + String(config.intervalMs) + " ms"
  );
  return true;
}

void sweepScannerPause()
{
  if (state == SWEEP_RUNNING) {
    state = SWEEP_PAUSED;
    midiScannerLog("CC sweep paused");
  }
}

void sweepScannerStop()
{
  if (state != SWEEP_STOPPED)
    midiScannerLog("CC sweep stopped");
  state = SWEEP_STOPPED;
  valueIndex = 0;
  currentChannel = currentConfig.firstChannel;
}

SweepState sweepScannerState()
{
  return state;
}

const SweepConfig &sweepScannerConfig()
{
  return currentConfig;
}

bool sweepScannerControlAllowed(uint8_t control)
{
  switch (control) {
    case 0:
    case 6:
    case 32:
    case 38:
    case 96:
    case 97:
    case 98:
    case 99:
    case 100:
    case 101:
    case 120:
    case 121:
    case 122:
    case 123:
    case 124:
    case 125:
    case 126:
    case 127:
      return false;
    default:
      return true;
  }
}

String sweepScannerBlockedReason(uint8_t control)
{
  if (sweepScannerControlAllowed(control))
    return "";

  return "CC " + String(control) +
         " is blocked: bank, RPN/NRPN, data-entry, reset, or all-notes command";
}