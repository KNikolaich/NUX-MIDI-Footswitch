#include "WatchyScreen.h"

#include <GxEPD2_BW.h>
#include <SPI.h>

#include "WatchyConfig.h"

namespace
{
  GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
    GxEPD2_154_D67(
      WatchyConfig::DISPLAY_CS_PIN,
      WatchyConfig::DISPLAY_DC_PIN,
      WatchyConfig::DISPLAY_RESET_PIN,
      WatchyConfig::DISPLAY_BUSY_PIN
    )
  );

  struct BatteryReading
  {
    float voltage;
    uint8_t percent;
    bool valid;
  };

  struct BatteryCurvePoint
  {
    float voltage;
    uint8_t percent;
  };

  constexpr BatteryCurvePoint BATTERY_CURVE[] = {
    {3.30f, 0},
    {3.50f, 5},
    {3.60f, 10},
    {3.70f, 20},
    {3.75f, 30},
    {3.80f, 40},
    {3.85f, 50},
    {3.90f, 60},
    {3.95f, 70},
    {4.00f, 80},
    {4.10f, 90},
    {4.20f, 100},
  };

  uint8_t batteryPercent(float voltage)
  {
    if (voltage <= BATTERY_CURVE[0].voltage)
      return 0;

    const size_t pointCount = sizeof(BATTERY_CURVE) / sizeof(BATTERY_CURVE[0]);
    for (size_t i = 1; i < pointCount; ++i) {
      const BatteryCurvePoint &lower = BATTERY_CURVE[i - 1];
      const BatteryCurvePoint &upper = BATTERY_CURVE[i];
      if (voltage <= upper.voltage) {
        const float fraction =
          (voltage - lower.voltage) / (upper.voltage - lower.voltage);
        return lower.percent +
          static_cast<uint8_t>((upper.percent - lower.percent) * fraction + 0.5f);
      }
    }

    return 100;
  }

  BatteryReading readBattery()
  {
    constexpr uint8_t SAMPLE_COUNT = 5;
    uint32_t totalMillivolts = 0;
    for (uint8_t i = 0; i < SAMPLE_COUNT; ++i) {
      totalMillivolts += analogReadMilliVolts(WatchyConfig::BATTERY_ADC_PIN);
      delay(2);
    }

    const float adcVoltage =
      (totalMillivolts / static_cast<float>(SAMPLE_COUNT)) / 1000.0f;
    const float batteryVoltage = adcVoltage * 2.0f;
    const bool valid = batteryVoltage >= 2.5f && batteryVoltage <= 4.5f;
    return {
      batteryVoltage,
      valid ? batteryPercent(batteryVoltage) : 0,
      valid
    };
  }

  void drawPresetButton(int16_t x, int16_t y, const char *label, uint8_t preset)
  {
    display.drawRect(x, y, 36, 32, GxEPD_BLACK);
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x + 4, y + 11);
    display.print(label);
    display.setCursor(x + 4, y + 25);
    display.print('=');
    display.print(preset);
  }

  void drawBatteryStatus(const BatteryReading &battery)
  {
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(104, 195);
    if (!battery.valid) {
      display.print("BAT --.-V --%");
      return;
    }

    display.print(battery.voltage <= 3.50f ? "LOW " : "BAT ");
    display.print(battery.voltage, 2);
    display.print("V ~");
    display.print(battery.percent);
    display.print('%');
  }
}

bool WatchyScreen::begin()
{
  SPI.begin(
    WatchyConfig::DISPLAY_SCK_PIN,
    WatchyConfig::DISPLAY_MISO_PIN,
    WatchyConfig::DISPLAY_MOSI_PIN,
    WatchyConfig::DISPLAY_CS_PIN
  );

  display.init(0, true, 10, false);
  display.setRotation(0);
  pinMode(WatchyConfig::BATTERY_ADC_PIN, INPUT);
  analogSetPinAttenuation(WatchyConfig::BATTERY_ADC_PIN, ADC_11db);
  _ready = true;
  return true;
}

void WatchyScreen::render(
  const DeviceSettings &settings,
  uint8_t currentPreset,
  bool bleConnected,
  bool portalActive)
{
  if (!_ready)
    return;

  if (currentPreset < 1 || currentPreset > WatchyConfig::PRESET_COUNT)
    currentPreset = 1;

  const BatteryReading battery = readBattery();
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.fillRect(0, 0, 200, 23, GxEPD_BLACK);
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(8, 16);
    display.print(portalActive ? "NUX SETUP / OTA ACTIVE" : "NUX MIDI / WATCHY V2.0");

    display.setTextColor(GxEPD_BLACK);
    drawPresetButton(4, 29, "P2", settings.presets[1]);
    drawPresetButton(160, 29, "P3", settings.presets[2]);
    drawPresetButton(4, 151, "P1", settings.presets[0]);
    drawPresetButton(160, 151, "P4", settings.presets[3]);

    display.setCursor(59, 49);
    display.print(bleConnected ? "BLE CONNECTED" : "BLE SEARCH");

    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(portalActive ? 3 : 5);
    display.setCursor(portalActive ? 82 : 70, portalActive ? 82 : 111);
    display.print('P');
    display.print(currentPreset);
    display.setTextSize(1);

    if (portalActive) {
      display.setCursor(5, 101);
      display.print("SSID: ");
      display.print(WatchyConfig::AP_SSID);
      display.setCursor(5, 117);
      display.print("PASS: ");
      display.print(WatchyConfig::AP_PASSWORD);
      display.setCursor(5, 133);
      display.print("IP: 192.168.4.1");
    }

    display.setCursor(5, 195);
    display.print(portalActive ? "WEB ON: BACK OFF" : "Hold BACK: setup");
    drawBatteryStatus(battery);
  } while (display.nextPage());

  // The e-paper retains its image without power. Shut down panel drive
  // voltages after the full-screen refresh.
  display.powerOff();
}