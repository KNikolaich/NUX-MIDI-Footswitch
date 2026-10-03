#pragma once

#include <Arduino.h>

#include "WatchyConfig.h"

struct DeviceSettings
{
  String bleTarget;
  uint8_t presets[WatchyConfig::BUTTON_COUNT];
};

class DeviceSettingsStore
{
public:
  bool begin();
  bool save(const String &bleTarget,
            const uint8_t presets[WatchyConfig::BUTTON_COUNT]);

  const DeviceSettings &get() const;

  // The BLE-MIDI transport copies its target during global construction.
  // Call this before BLEMIDI_CREATE_INSTANCE.
  static String loadBleTargetEarly();

private:
  DeviceSettings _settings;
};