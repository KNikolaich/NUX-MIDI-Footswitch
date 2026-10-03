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

  constexpr int16_t DISPLAY_WIDTH = 200;
  constexpr int16_t HEADER_HEIGHT = 35;

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

  void drawPresetLabel(int16_t x, int16_t y, const char *label, uint8_t preset)
  {
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x + 4, y + 11);
    display.print(label);
    display.setCursor(x + 4, y + 25);
    display.print('=');
    display.print(preset);
  }

  void drawHeader(const char *text)
  {
    // Scale the built-in 5x7 font to about 1.5x vertically and 1.4x
    // horizontally so the full title still fits on the 200px display.
    GFXcanvas1 textCanvas(DISPLAY_WIDTH, 8);
    textCanvas.fillScreen(0);
    textCanvas.setTextSize(1);
    textCanvas.setTextColor(1);
    textCanvas.setCursor(0, 0);
    textCanvas.print(text);

    int16_t sourceWidth = 0;
    for (const char *character = text; *character; ++character)
      sourceWidth += 6;
    const int16_t scaledWidth = (sourceWidth * 7 + 4) / 5;
    constexpr int16_t scaledHeight = 12;
    const int16_t originX = (DISPLAY_WIDTH - scaledWidth) / 2;
    const int16_t originY = (HEADER_HEIGHT - scaledHeight) / 2;

    display.fillRect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, GxEPD_BLACK);
    for (int16_t y = 0; y < 8; ++y) {
      const int16_t y0 = originY + (y * 3) / 2;
      const int16_t y1 = originY + ((y + 1) * 3) / 2;
      for (int16_t x = 0; x < sourceWidth; ++x) {
        if (!textCanvas.getPixel(x, y))
          continue;
        const int16_t x0 = originX + (x * 7) / 5;
        const int16_t x1 = originX + ((x + 1) * 7) / 5;
        display.fillRect(x0, y0, x1 - x0, y1 - y0, GxEPD_WHITE);
      }
    }
  }

  void drawBleStatus(bool bleConnected)
  {
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(59, 49);
    display.print(bleConnected ? "BLE CONNECTED" : "BLE SEARCH");
  }

  void drawCurrentPreset(uint8_t currentPreset, bool portalActive)
  {
    display.setFont(nullptr);
    display.setTextColor(GxEPD_BLACK);
    if (portalActive) {
      display.setTextSize(3);
      display.setCursor(82, 68);
    } else {
      display.setTextSize(5);
      display.setCursor(70, 111);
    }
    display.print('P');
    display.print(currentPreset);
    display.setTextSize(1);
  }

  void drawBatteryStatus(const BatteryReading &battery)
  {
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(104, 191);
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

  void renderPartial(uint8_t currentPreset, bool bleConnected, bool portalActive)
  {
    constexpr int16_t BLE_X = 56;
    constexpr int16_t BLE_Y = 43;
    constexpr int16_t BLE_W = 88;
    constexpr int16_t BLE_H = 17;
    display.setPartialWindow(BLE_X, BLE_Y, BLE_W, BLE_H);
    display.firstPage();
    do {
      display.fillRect(BLE_X, BLE_Y, BLE_W, BLE_H, GxEPD_WHITE);
      drawBleStatus(bleConnected);
    } while (display.nextPage());

    if (portalActive) {
      constexpr int16_t PRESET_X = 76;
      constexpr int16_t PRESET_Y = 64;
      constexpr int16_t PRESET_W = 48;
      constexpr int16_t PRESET_H = 32;
      display.setPartialWindow(PRESET_X, PRESET_Y, PRESET_W, PRESET_H);
      display.firstPage();
      do {
        display.fillRect(PRESET_X, PRESET_Y, PRESET_W, PRESET_H, GxEPD_WHITE);
        drawCurrentPreset(currentPreset, true);
      } while (display.nextPage());
    } else {
      constexpr int16_t PRESET_X = 64;
      constexpr int16_t PRESET_Y = 106;
      constexpr int16_t PRESET_W = 72;
      constexpr int16_t PRESET_H = 52;
      display.setPartialWindow(PRESET_X, PRESET_Y, PRESET_W, PRESET_H);
      display.firstPage();
      do {
        display.fillRect(PRESET_X, PRESET_Y, PRESET_W, PRESET_H, GxEPD_WHITE);
        drawCurrentPreset(currentPreset, false);
      } while (display.nextPage());
    }
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
  bool portalActive,
  bool fullRefresh)
{
  if (!_ready)
    return;

  if (currentPreset < 1 || currentPreset > WatchyConfig::PRESET_COUNT)
    currentPreset = 1;

  if (!fullRefresh) {
    renderPartial(currentPreset, bleConnected, portalActive);
    display.powerOff();
    return;
  }

  const BatteryReading battery = readBattery();
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    drawHeader(portalActive ? "NUX SETUP / OTA ACTIVE" : "NUX MIDI / WATCHY V2.0");

    display.setTextColor(GxEPD_BLACK);
    drawPresetLabel(4, 29, "P2", settings.presets[1]);
    drawPresetLabel(160, 29, "P3", settings.presets[2]);
    drawPresetLabel(4, 151, "P1", settings.presets[0]);
    drawPresetLabel(160, 151, "P4", settings.presets[3]);

    drawBleStatus(bleConnected);

    drawCurrentPreset(currentPreset, portalActive);

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

    display.setCursor(5, 191);
    display.print(portalActive ? "WEB ON: BACK OFF" : "Hold BACK: setup");
    drawBatteryStatus(battery);
  } while (display.nextPage());

  // The e-paper retains its image without power. Shut down panel drive
  // voltages after the full-screen refresh.
  display.powerOff();
}