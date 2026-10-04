#include "WatchyScreen.h"

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#include "FirmwareVersion.h"
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

  void drawPresetLabel(
    int16_t x,
    int16_t y,
    const char *label,
    uint8_t preset,
    DoubleTapAction doubleTapAction,
    uint8_t doubleTapPreset)
  {
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x + 4, y + 11);
    display.print(label);
    display.print('=');
    display.print(preset);

    display.setCursor(x + 4, y + 25);
    switch (doubleTapAction) {
      case DoubleTapAction::Disabled:
        display.print("2x:OFF");
        break;
      case DoubleTapAction::CycleNext:
        display.print("2x->");
        display.print((preset % WatchyConfig::PRESET_COUNT) + 1);
        break;
      case DoubleTapAction::FixedPreset:
        display.print("2x->");
        display.print(doubleTapPreset);
        break;
      default:
        display.print("2x:?");
        break;
    }
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
      display.setTextSize(4);
      display.setCursor(76, 61);
    } else {
      display.setTextSize(7);
      display.setCursor(58, 102);
    }
    display.print('P');
    display.print(currentPreset);
    display.setTextSize(1);
  }

  void drawBatteryStatus(const BatteryReading &battery)
  {
    constexpr int16_t ICON_X = 150;
    constexpr int16_t ICON_Y = 180;
    char percentText[5];
    if (battery.valid)
      snprintf(percentText, sizeof(percentText), "%u%%",
        static_cast<unsigned>(battery.percent));
    else
      snprintf(percentText, sizeof(percentText), "--%%");

    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    const int16_t percentageWidth =
      static_cast<int16_t>(strlen(percentText) * 6);
    display.setCursor(ICON_X - 8 - percentageWidth, 186);
    display.print(percentText);

    display.drawRoundRect(ICON_X, ICON_Y, 38, 19, 3, GxEPD_BLACK);
    display.fillRect(ICON_X + 38, ICON_Y + 6, 5, 7, GxEPD_BLACK);

    uint8_t bars = 0;
    if (battery.valid)
      bars = (battery.percent + 24) / 25;
    for (uint8_t i = 0; i < bars; ++i)
      display.fillRect(ICON_X + 5 + i * 7, ICON_Y + 5, 5, 9, GxEPD_BLACK);
  }

  void drawCompleteScreen(
    const DeviceSettings &settings,
    uint8_t currentPreset,
    bool bleConnected,
    bool portalActive,
    const BatteryReading &battery)
  {
    display.fillScreen(GxEPD_WHITE);

    char header[32];
    if (portalActive)
      snprintf(header, sizeof(header), "FW v%s / SETUP", FirmwareVersion::STRING);
    else
      snprintf(header, sizeof(header), "NUX MIDI / FW v%s", FirmwareVersion::STRING);
    drawHeader(header);

    display.setTextColor(GxEPD_BLACK);
    drawPresetLabel(
      4, 29, "P2", settings.presets[1],
      settings.doubleTapActions[1], settings.doubleTapPresets[1]);
    drawPresetLabel(
      160, 29, "P3", settings.presets[2],
      settings.doubleTapActions[2], settings.doubleTapPresets[2]);
    drawPresetLabel(
      4, 132, "P1", settings.presets[0],
      settings.doubleTapActions[0], settings.doubleTapPresets[0]);
    drawPresetLabel(
      160, 132, "P4", settings.presets[3],
      settings.doubleTapActions[3], settings.doubleTapPresets[3]);

    drawBleStatus(bleConnected);
    drawCurrentPreset(currentPreset, portalActive);

    if (portalActive) {
      display.setTextSize(1);
      display.setCursor(5, 101);
      display.print("SSID: ");
      display.print(WatchyConfig::AP_SSID);
      display.setCursor(5, 117);
      display.print("PASS: ");
      display.print(WatchyConfig::AP_PASSWORD);
      display.setCursor(5, 133);
      display.print("IP: 192.168.4.1");
    }

    display.setTextSize(1);
    display.setCursor(5, 191);
    display.print(portalActive ? "WEB ON: BACK OFF" : "Hold BACK: setup");
    drawBatteryStatus(battery);
  }

  void drawSleepIllustration()
  {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(nullptr);
    display.setTextColor(GxEPD_BLACK);

    // Pillow, with a second outline to suggest a soft cushion.
    display.drawRoundRect(22, 148, 156, 36, 17, GxEPD_BLACK);
    display.drawRoundRect(28, 153, 144, 26, 13, GxEPD_BLACK);
    display.drawLine(39, 164, 48, 168, GxEPD_BLACK);
    display.drawLine(48, 168, 56, 164, GxEPD_BLACK);
    display.drawLine(145, 164, 153, 168, GxEPD_BLACK);
    display.drawLine(153, 168, 162, 164, GxEPD_BLACK);

    // Curled-up body.
    display.fillRoundRect(78, 86, 103, 65, 30, GxEPD_WHITE);
    display.drawRoundRect(78, 86, 103, 65, 30, GxEPD_BLACK);

    // Head and pointed ears, drawn over the body to keep the silhouette clean.
    display.fillCircle(79, 88, 36, GxEPD_WHITE);
    display.drawCircle(79, 88, 36, GxEPD_BLACK);
    display.fillTriangle(48, 76, 54, 34, 82, 62, GxEPD_WHITE);
    display.drawTriangle(48, 76, 54, 34, 82, 62, GxEPD_BLACK);
    display.fillTriangle(99, 61, 127, 31, 133, 77, GxEPD_WHITE);
    display.drawTriangle(99, 61, 127, 31, 133, 77, GxEPD_BLACK);
    display.drawTriangle(57, 63, 59, 45, 73, 62, GxEPD_BLACK);
    display.drawTriangle(107, 62, 123, 43, 127, 67, GxEPD_BLACK);

    // Closed eyes and a small muzzle.
    display.drawLine(59, 87, 63, 93, GxEPD_BLACK);
    display.drawLine(63, 93, 69, 95, GxEPD_BLACK);
    display.drawLine(69, 95, 75, 90, GxEPD_BLACK);
    display.drawLine(87, 90, 93, 95, GxEPD_BLACK);
    display.drawLine(93, 95, 99, 93, GxEPD_BLACK);
    display.drawLine(99, 93, 103, 87, GxEPD_BLACK);
    display.fillTriangle(74, 101, 86, 101, 80, 107, GxEPD_BLACK);
    display.drawLine(80, 107, 80, 112, GxEPD_BLACK);
    display.drawLine(80, 112, 74, 116, GxEPD_BLACK);
    display.drawLine(80, 112, 87, 116, GxEPD_BLACK);

    // Paws and a curled tail on the side of the body.
    display.fillRoundRect(43, 126, 39, 19, 9, GxEPD_WHITE);
    display.drawRoundRect(43, 126, 39, 19, 9, GxEPD_BLACK);
    display.fillRoundRect(99, 137, 44, 18, 9, GxEPD_WHITE);
    display.drawRoundRect(99, 137, 44, 18, 9, GxEPD_BLACK);
    display.drawLine(143, 103, 154, 108, GxEPD_BLACK);
    display.drawLine(154, 108, 160, 117, GxEPD_BLACK);
    display.drawLine(160, 117, 157, 127, GxEPD_BLACK);
    display.drawLine(157, 127, 148, 132, GxEPD_BLACK);
    display.drawLine(148, 132, 140, 128, GxEPD_BLACK);

    // Sleep marks in the upper-right corner.
    display.setTextSize(2);
    display.setCursor(145, 34);
    display.print('Z');
    display.setTextSize(1);
    display.setCursor(166, 48);
    display.print('z');
    display.setCursor(177, 37);
    display.print('z');

    display.setTextSize(1);
    display.setCursor(69, 190);
    display.print("DEEP SLEEP");
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

  const BatteryReading battery = readBattery();
  if (fullRefresh)
    display.setFullWindow();
  else
    display.setPartialWindow(0, 0, DISPLAY_WIDTH, GxEPD2_154_D67::HEIGHT);

  display.firstPage();
  do {
    drawCompleteScreen(
      settings,
      currentPreset,
      bleConnected,
      portalActive,
      battery
    );
  } while (display.nextPage());

  // The full frame was redrawn above. A partial waveform updates the entire
  // panel for ordinary changes; only the scheduled/AP path uses full refresh.
  display.powerOff();
}

void WatchyScreen::renderSleepScreen()
{
  if (!_ready)
    return;

  display.setFullWindow();
  display.firstPage();
  do {
    drawSleepIllustration();
  } while (display.nextPage());
  display.powerOff();
}