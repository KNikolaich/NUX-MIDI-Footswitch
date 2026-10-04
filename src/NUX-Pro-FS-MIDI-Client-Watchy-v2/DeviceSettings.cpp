#include "DeviceSettings.h"

#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace
{
  constexpr char NVS_NAMESPACE[] = "watchynux";
  constexpr char TARGET_KEY[] = "target";
  constexpr char PRESET_KEYS[WatchyConfig::BUTTON_COUNT][9] = {
    "menu", "back", "up", "down"
  };
  constexpr char DOUBLE_TAP_ACTION_KEYS[WatchyConfig::BUTTON_COUNT][12] = {
    "menudblmode", "backdblmode", "updblmode", "downdblmode"
  };
  constexpr char DOUBLE_TAP_PRESET_KEYS[WatchyConfig::BUTTON_COUNT][14] = {
    "menudblpreset", "backdblpreset", "updblpreset", "downdblpreset"
  };
  constexpr uint8_t DEFAULT_PRESETS[WatchyConfig::BUTTON_COUNT] = {
    1, 2, 3, 4
  };
  constexpr size_t MAX_BLE_TARGET_LENGTH = 22;

  Preferences preferences;

  bool validBleTarget(const String &target)
  {
    if (target.length() == 0 || target.length() > MAX_BLE_TARGET_LENGTH)
      return false;

    for (size_t i = 0; i < target.length(); i++) {
      const unsigned char ch = target[i];
      if (ch < 0x20 || ch > 0x7e)
        return false;
    }
    return true;
  }
}

String DeviceSettingsStore::loadBleTargetEarly()
{
  if (nvs_flash_init() != ESP_OK)
    return String(WatchyConfig::DEFAULT_BLE_TARGET);

  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return String(WatchyConfig::DEFAULT_BLE_TARGET);

  size_t length = 0;
  if (nvs_get_str(handle, TARGET_KEY, nullptr, &length) != ESP_OK ||
      length < 2 || length > MAX_BLE_TARGET_LENGTH + 1) {
    nvs_close(handle);
    return String(WatchyConfig::DEFAULT_BLE_TARGET);
  }

  char target[MAX_BLE_TARGET_LENGTH + 1] = {};
  const esp_err_t result = nvs_get_str(handle, TARGET_KEY, target, &length);
  nvs_close(handle);

  if (result != ESP_OK)
    return String(WatchyConfig::DEFAULT_BLE_TARGET);

  String loadedTarget(target);
  if (!validBleTarget(loadedTarget))
    return String(WatchyConfig::DEFAULT_BLE_TARGET);
  return loadedTarget;
}

bool DeviceSettingsStore::begin()
{
  _settings.bleTarget = WatchyConfig::DEFAULT_BLE_TARGET;
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++)
    _settings.presets[i] = DEFAULT_PRESETS[i];
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    _settings.doubleTapActions[i] = DoubleTapAction::CycleNext;
    _settings.doubleTapPresets[i] = DEFAULT_PRESETS[i];
  }

  // Open writable so a clean device creates the namespace on first boot.
  if (!preferences.begin(NVS_NAMESPACE, false))
    return false;

  const String savedTarget =
    preferences.getString(TARGET_KEY, WatchyConfig::DEFAULT_BLE_TARGET);
  if (validBleTarget(savedTarget))
    _settings.bleTarget = savedTarget;

  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    const uint8_t preset =
      preferences.getUChar(PRESET_KEYS[i], DEFAULT_PRESETS[i]);
    if (preset >= 1 && preset <= WatchyConfig::PRESET_COUNT)
      _settings.presets[i] = preset;

    const uint8_t action = preferences.getUChar(
      DOUBLE_TAP_ACTION_KEYS[i],
      static_cast<uint8_t>(DoubleTapAction::CycleNext));
    if (action <= static_cast<uint8_t>(DoubleTapAction::FixedPreset))
      _settings.doubleTapActions[i] = static_cast<DoubleTapAction>(action);

    const uint8_t doubleTapPreset = preferences.getUChar(
      DOUBLE_TAP_PRESET_KEYS[i],
      _settings.presets[i]);
    if (doubleTapPreset >= 1 &&
        doubleTapPreset <= WatchyConfig::PRESET_COUNT)
      _settings.doubleTapPresets[i] = doubleTapPreset;
  }

  preferences.end();
  return true;
}

bool DeviceSettingsStore::save(
  const String &bleTarget,
  const uint8_t presets[WatchyConfig::BUTTON_COUNT],
  const DoubleTapAction doubleTapActions[WatchyConfig::BUTTON_COUNT],
  const uint8_t doubleTapPresets[WatchyConfig::BUTTON_COUNT])
{
  if (!validBleTarget(bleTarget))
    return false;
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    if (presets[i] < 1 || presets[i] > WatchyConfig::PRESET_COUNT)
      return false;
    if (static_cast<uint8_t>(doubleTapActions[i]) >
          static_cast<uint8_t>(DoubleTapAction::FixedPreset) ||
        doubleTapPresets[i] < 1 ||
        doubleTapPresets[i] > WatchyConfig::PRESET_COUNT)
      return false;
  }

  if (!preferences.begin(NVS_NAMESPACE, false))
    return false;

  bool ok = preferences.putString(TARGET_KEY, bleTarget) ==
            bleTarget.length();
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    if (preferences.putUChar(PRESET_KEYS[i], presets[i]) !=
        sizeof(presets[i])) {
      ok = false;
    }
    const uint8_t action = static_cast<uint8_t>(doubleTapActions[i]);
    if (preferences.putUChar(DOUBLE_TAP_ACTION_KEYS[i], action) !=
        sizeof(action)) {
      ok = false;
    }
    if (preferences.putUChar(
          DOUBLE_TAP_PRESET_KEYS[i], doubleTapPresets[i]) !=
        sizeof(doubleTapPresets[i])) {
      ok = false;
    }
  }
  preferences.end();

  if (!ok)
    return false;

  _settings.bleTarget = bleTarget;
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    _settings.presets[i] = presets[i];
    _settings.doubleTapActions[i] = doubleTapActions[i];
    _settings.doubleTapPresets[i] = doubleTapPresets[i];
  }
  return true;
}

const DeviceSettings &DeviceSettingsStore::get() const
{
  return _settings;
}