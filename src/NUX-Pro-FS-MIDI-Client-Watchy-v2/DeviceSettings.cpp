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
  }

  preferences.end();
  return true;
}

bool DeviceSettingsStore::save(
  const String &bleTarget,
  const uint8_t presets[WatchyConfig::BUTTON_COUNT])
{
  if (!validBleTarget(bleTarget))
    return false;
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    if (presets[i] < 1 || presets[i] > WatchyConfig::PRESET_COUNT)
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
  }
  preferences.end();

  if (!ok)
    return false;

  _settings.bleTarget = bleTarget;
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++)
    _settings.presets[i] = presets[i];
  return true;
}

const DeviceSettings &DeviceSettingsStore::get() const
{
  return _settings;
}