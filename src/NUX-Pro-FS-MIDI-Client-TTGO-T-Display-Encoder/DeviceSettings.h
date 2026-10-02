#pragma once

#include <Arduino.h>

struct DeviceSettings
{
  String bleTarget;
  uint8_t presetA;
  uint8_t presetB;
};

String deviceSettingsLoadBleTargetEarly();
bool deviceSettingsBegin(DeviceSettings &settings);
bool deviceSettingsSave(
  DeviceSettings &settings,
  const String &bleTarget,
  uint8_t presetA,
  uint8_t presetB
);