#pragma once

#include <Arduino.h>

#include "DeviceSettings.h"

class WatchyScreen
{
public:
  bool begin();
  void render(const DeviceSettings &settings,
              uint8_t currentPreset,
              bool bleConnected,
              bool portalActive,
              bool fullRefresh);
  void renderSleepScreen();

private:
  bool _ready = false;
};