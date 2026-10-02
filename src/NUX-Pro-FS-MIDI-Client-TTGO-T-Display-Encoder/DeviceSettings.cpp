#include "DeviceSettings.h"

#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace
{
  constexpr char NVS_NAMESPACE[] = "nuxttgo";
  constexpr char DEFAULT_BLE_TARGET[] = "cb:4e:fd:a3:6c:1b";
  constexpr uint8_t DEFAULT_PRESET_A = 1;
  constexpr uint8_t DEFAULT_PRESET_B = 3;
  constexpr size_t MAX_BLE_TARGET_LENGTH = 22;

  bool validTarget(const String &target)
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

  Preferences preferences;
}

String deviceSettingsLoadBleTargetEarly()
{
  // BLE-MIDI copies the target name/address into its transport at global
  // construction time, so it must be read before BLEMIDI_CREATE_INSTANCE.
  if (nvs_flash_init() != ESP_OK)
    return String(DEFAULT_BLE_TARGET);

  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return String(DEFAULT_BLE_TARGET);

  size_t length = 0;
  if (nvs_get_str(handle, "target", nullptr, &length) != ESP_OK ||
      length < 2 ||
      length > MAX_BLE_TARGET_LENGTH + 1) {
    nvs_close(handle);
    return String(DEFAULT_BLE_TARGET);
  }

  char target[MAX_BLE_TARGET_LENGTH + 1] = {};
  const esp_err_t result = nvs_get_str(handle, "target", target, &length);
  nvs_close(handle);

  if (result != ESP_OK || target[0] == '\0')
    return String(DEFAULT_BLE_TARGET);
  return String(target);
}

bool deviceSettingsBegin(DeviceSettings &settings)
{
  settings.bleTarget = DEFAULT_BLE_TARGET;
  settings.presetA = DEFAULT_PRESET_A;
  settings.presetB = DEFAULT_PRESET_B;

  if (!preferences.begin(NVS_NAMESPACE, false))
    return false;

  const String storedTarget =
    preferences.getString("target", DEFAULT_BLE_TARGET);
  if (validTarget(storedTarget))
    settings.bleTarget = storedTarget;

  const uint8_t presetA = preferences.getUChar("presetA", DEFAULT_PRESET_A);
  const uint8_t presetB = preferences.getUChar("presetB", DEFAULT_PRESET_B);
  if (presetA >= 1 && presetA <= 7)
    settings.presetA = presetA;
  if (presetB >= 1 && presetB <= 7)
    settings.presetB = presetB;

  preferences.end();
  return true;
}

bool deviceSettingsSave(
  DeviceSettings &settings,
  const String &bleTarget,
  uint8_t presetA,
  uint8_t presetB
)
{
  if (!validTarget(bleTarget) ||
      presetA < 1 || presetA > 7 ||
      presetB < 1 || presetB > 7)
    return false;

  if (!preferences.begin(NVS_NAMESPACE, false))
    return false;

  const bool ok =
    preferences.putString("target", bleTarget) == bleTarget.length() &&
    preferences.putUChar("presetA", presetA) == sizeof(presetA) &&
    preferences.putUChar("presetB", presetB) == sizeof(presetB);
  preferences.end();

  if (ok) {
    settings.bleTarget = bleTarget;
    settings.presetA = presetA;
    settings.presetB = presetB;
  }
  return ok;
}
