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

  void drawQuadraticCurve(
    int16_t startX,
    int16_t startY,
    int16_t controlX,
    int16_t controlY,
    int16_t endX,
    int16_t endY)
  {
    constexpr uint8_t CURVE_STEPS = 10;
    int16_t previousX = startX;
    int16_t previousY = startY;
    for (uint8_t step = 1; step <= CURVE_STEPS; ++step) {
      const float t = step / static_cast<float>(CURVE_STEPS);
      const float inverseT = 1.0f - t;
      const int16_t x = static_cast<int16_t>(
        inverseT * inverseT * startX +
        2.0f * inverseT * t * controlX +
        t * t * endX + 0.5f);
      const int16_t y = static_cast<int16_t>(
        inverseT * inverseT * startY +
        2.0f * inverseT * t * controlY +
        t * t * endY + 0.5f);
      display.drawLine(previousX, previousY, x, y, GxEPD_BLACK);
      previousX = x;
      previousY = y;
    }
  }

  bool polygonContainsPoint(
    int16_t x,
    int16_t y,
    const int16_t *vertices,
    uint8_t pointCount)
  {
    bool inside = false;
    for (uint8_t i = 0, j = pointCount - 1; i < pointCount; j = i++) {
      const int16_t xi = vertices[i * 2];
      const int16_t yi = vertices[i * 2 + 1];
      const int16_t xj = vertices[j * 2];
      const int16_t yj = vertices[j * 2 + 1];
      if ((yi > y) != (yj > y)) {
        const int32_t intersectionX =
          xi + static_cast<int32_t>(xj - xi) * (y - yi) / (yj - yi);
        if (x < intersectionX)
          inside = !inside;
      }
    }
    return inside;
  }

  void fillPatternPolygon(
    const int16_t *vertices,
    uint8_t pointCount,
    uint16_t color,
    uint8_t densityThreshold)
  {
    static constexpr uint8_t BAYER_4X4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
    };

    int16_t minX = vertices[0];
    int16_t maxX = vertices[0];
    int16_t minY = vertices[1];
    int16_t maxY = vertices[1];
    for (uint8_t i = 1; i < pointCount; ++i) {
      minX = min(minX, vertices[i * 2]);
      maxX = max(maxX, vertices[i * 2]);
      minY = min(minY, vertices[i * 2 + 1]);
      maxY = max(maxY, vertices[i * 2 + 1]);
    }

    for (int16_t y = minY; y <= maxY; ++y) {
      for (int16_t x = minX; x <= maxX; ++x) {
        if (!polygonContainsPoint(x, y, vertices, pointCount))
          continue;

        if (color == GxEPD_WHITE ||
            BAYER_4X4[y & 3][x & 3] < densityThreshold) {
          display.drawPixel(x, y, color);
        }
      }
    }
  }

  void drawPolygonOutline(const int16_t *vertices, uint8_t pointCount)
  {
    for (uint8_t i = 0; i < pointCount; ++i) {
      const uint8_t next = (i + 1) % pointCount;
      display.drawLine(
        vertices[i * 2],
        vertices[i * 2 + 1],
        vertices[next * 2],
        vertices[next * 2 + 1],
        GxEPD_BLACK
      );
    }
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

    // 4x4 ordered dithering approximates 30% orange and 50% red on the
    // monochrome panel: thresholds 5/16 and 8/16, respectively.
    constexpr uint8_t ORANGE_DENSITY = 5;
    constexpr uint8_t RED_DENSITY = 8;

    // Small raised tail behind the rump.
    const int16_t tail[] = {150, 99, 153, 91, 158, 85, 163, 97, 159, 104};
    fillPatternPolygon(
      tail, sizeof(tail) / sizeof(tail[0]) / 2, GxEPD_BLACK, ORANGE_DENSITY);
    drawPolygonOutline(tail, sizeof(tail) / sizeof(tail[0]) / 2);

    // Low, elongated body with a stippled orange back and white underside.
    const int16_t body[] = {
      77, 110, 78, 101, 83, 94, 92, 89, 105, 87, 123, 88, 138, 91,
      149, 96, 157, 103, 162, 113, 164, 122, 161, 131, 154, 137,
      143, 141, 128, 143, 111, 143, 97, 139, 85, 133, 78, 126, 75, 118
    };
    fillPatternPolygon(
      body, sizeof(body) / sizeof(body[0]) / 2, GxEPD_WHITE, 16);
    const int16_t orangeBack[] = {
      81, 110, 82, 101, 87, 96, 95, 91, 107, 89, 123, 90, 138, 93,
      148, 98, 155, 105, 159, 113, 159, 119, 151, 122, 141, 123,
      128, 122, 114, 120, 101, 119, 90, 118, 83, 116
    };
    fillPatternPolygon(
      orangeBack,
      sizeof(orangeBack) / sizeof(orangeBack[0]) / 2,
      GxEPD_BLACK,
      ORANGE_DENSITY
    );
    drawPolygonOutline(body, sizeof(body) / sizeof(body[0]) / 2);

    // Ears sit behind the head. One inner ear has the denser red/pink pattern.
    const int16_t farEar[] = {
      46, 91, 42, 69, 44, 63, 48, 60, 53, 64, 64, 86
    };
    fillPatternPolygon(
      farEar, sizeof(farEar) / sizeof(farEar[0]) / 2,
      GxEPD_WHITE, 16);
    const int16_t farEarOrange[] = {
      46, 86, 44, 69, 46, 64, 49, 63, 53, 67, 61, 85
    };
    fillPatternPolygon(
      farEarOrange,
      sizeof(farEarOrange) / sizeof(farEarOrange[0]) / 2,
      GxEPD_BLACK,
      ORANGE_DENSITY
    );
    drawPolygonOutline(farEar, sizeof(farEar) / sizeof(farEar[0]) / 2);

    const int16_t nearEar[] = {
      71, 88, 78, 64, 81, 58, 85, 54, 90, 59, 97, 91
    };
    fillPatternPolygon(
      nearEar, sizeof(nearEar) / sizeof(nearEar[0]) / 2,
      GxEPD_WHITE, 16);
    const int16_t nearEarOrange[] = {
      75, 83, 80, 65, 83, 58, 86, 59, 90, 63, 94, 86
    };
    fillPatternPolygon(
      nearEarOrange,
      sizeof(nearEarOrange) / sizeof(nearEarOrange[0]) / 2,
      GxEPD_BLACK,
      ORANGE_DENSITY
    );
    const int16_t redInnerEar[] = {
      79, 79, 83, 63, 85, 60, 88, 64, 91, 81
    };
    fillPatternPolygon(
      redInnerEar,
      sizeof(redInnerEar) / sizeof(redInnerEar[0]) / 2,
      GxEPD_BLACK,
      RED_DENSITY
    );
    drawPolygonOutline(nearEar, sizeof(nearEar) / sizeof(nearEar[0]) / 2);

    // Side-profile head with a white muzzle and blaze.
    const int16_t head[] = {
      50, 103, 47, 98, 48, 91, 52, 84, 58, 79, 66, 76, 74, 77,
      82, 80, 89, 86, 95, 94, 98, 102, 98, 109, 94, 116, 87, 122,
      78, 126, 68, 127, 59, 124, 52, 121, 47, 117, 42, 114,
      39, 111, 39, 107, 42, 104, 47, 102
    };
    fillPatternPolygon(
      head, sizeof(head) / sizeof(head[0]) / 2, GxEPD_WHITE, 16);
    const int16_t leftOrange[] = {
      48, 103, 47, 96, 51, 88, 58, 82, 66, 78, 73, 78, 78, 83,
      75, 90, 70, 97, 64, 102, 56, 106, 50, 108
    };
    fillPatternPolygon(
      leftOrange,
      sizeof(leftOrange) / sizeof(leftOrange[0]) / 2,
      GxEPD_BLACK,
      ORANGE_DENSITY
    );
    const int16_t rightOrange[] = {
      75, 78, 82, 81, 89, 87, 94, 94, 97, 102, 95, 109,
      90, 113, 84, 110, 84, 103, 87, 98, 82, 92, 78, 88
    };
    fillPatternPolygon(
      rightOrange,
      sizeof(rightOrange) / sizeof(rightOrange[0]) / 2,
      GxEPD_BLACK,
      ORANGE_DENSITY
    );
    const int16_t whiteBlaze[] = {
      67, 78, 72, 78, 77, 83, 75, 90, 71, 97, 66, 103,
      60, 108, 55, 108, 60, 101, 64, 94, 65, 86
    };
    fillPatternPolygon(
      whiteBlaze, sizeof(whiteBlaze) / sizeof(whiteBlaze[0]) / 2,
      GxEPD_WHITE, 16);
    drawPolygonOutline(head, sizeof(head) / sizeof(head[0]) / 2);

    // White paws tucked under the chest and at the rear.
    const int16_t frontPaw[] = {
      88, 124, 94, 126, 100, 130, 106, 136, 106, 141,
      102, 144, 95, 143, 90, 139, 87, 133
    };
    fillPatternPolygon(
      frontPaw, sizeof(frontPaw) / sizeof(frontPaw[0]) / 2,
      GxEPD_WHITE, 16);
    drawPolygonOutline(frontPaw, sizeof(frontPaw) / sizeof(frontPaw[0]) / 2);
    drawQuadraticCurve(95, 138, 96, 141, 98, 142);
    drawQuadraticCurve(101, 137, 101, 140, 100, 142);

    const int16_t rearPaw[] = {
      139, 129, 146, 128, 153, 130, 157, 134, 156, 138,
      151, 141, 143, 140, 138, 136
    };
    fillPatternPolygon(
      rearPaw, sizeof(rearPaw) / sizeof(rearPaw[0]) / 2,
      GxEPD_WHITE, 16);
    drawPolygonOutline(rearPaw, sizeof(rearPaw) / sizeof(rearPaw[0]) / 2);
    display.drawLine(148, 136, 147, 139, GxEPD_BLACK);
    display.drawLine(152, 136, 151, 139, GxEPD_BLACK);

    // Sleeping side-profile face: closed eye, nose, mouth and a pink tongue.
    drawQuadraticCurve(59, 96, 63, 100, 68, 97);
    display.fillCircle(40, 107, 3, GxEPD_BLACK);
    display.drawLine(41, 109, 44, 112, GxEPD_BLACK);
    display.drawLine(44, 112, 50, 113, GxEPD_BLACK);
    const int16_t tongue[] = {47, 113, 53, 113, 55, 116, 53, 119, 49, 119, 47, 116};
    fillPatternPolygon(
      tongue, sizeof(tongue) / sizeof(tongue[0]) / 2,
      GxEPD_BLACK, RED_DENSITY);
    drawPolygonOutline(tongue, sizeof(tongue) / sizeof(tongue[0]) / 2);

    // "Zzz" sits above the dog's head.
    display.setTextSize(2);
    display.setCursor(112, 37);
    display.print('Z');
    display.setTextSize(1);
    display.setCursor(132, 52);
    display.print('z');
    display.setCursor(144, 43);
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

  // The full frame was redrawn above. Non-preset status changes may use the
  // fast partial waveform; the caller selects full refresh after preset changes.
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