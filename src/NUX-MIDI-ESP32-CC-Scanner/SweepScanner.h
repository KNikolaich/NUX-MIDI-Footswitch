#pragma once

#include <Arduino.h>

enum SweepState : uint8_t
{
  SWEEP_STOPPED = 0,
  SWEEP_RUNNING = 1,
  SWEEP_PAUSED = 2
};

struct SweepConfig
{
  uint8_t control;
  uint8_t firstChannel;
  uint8_t lastChannel;
  uint16_t intervalMs;
  String values;
};

void sweepScannerBegin();
void sweepScannerLoop();

bool sweepScannerStart(const SweepConfig &config, String &error);
void sweepScannerPause();
void sweepScannerStop();
SweepState sweepScannerState();
const SweepConfig &sweepScannerConfig();

bool sweepScannerControlAllowed(uint8_t control);
String sweepScannerBlockedReason(uint8_t control);