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

  const bool fullRefresh =
    _renderCount == 0 ||
    _renderCount % WatchyConfig::FULL_DISPLAY_REFRESH_EVERY == 0;

  if (fullRefresh)
    display.setFullWindow();
  else
    display.setPartialWindow(0, 0, 200, 200);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.fillRect(0, 0, 200, 26, GxEPD_BLACK);
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(8, 18);
    display.print("NUX MIDI / WATCHY V2.0");

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(8, 43);
    display.print("BLE: ");
    display.print(bleConnected ? "CONNECTED" : "SEARCHING");

    display.setTextSize(5);
    display.setCursor(67, 99);
    display.print('P');
    display.print(currentPreset);
    display.setTextSize(1);

    display.setCursor(8, 120);
    display.print("MENU 26: P");
    display.print(settings.presets[0]);
    display.setCursor(8, 137);
    display.print("BACK 25: P");
    display.print(settings.presets[1]);
    display.setCursor(8, 154);
    display.print("UP   35: P");
    display.print(settings.presets[2]);
    display.setCursor(8, 171);
    display.print("DOWN  4: P");
    display.print(settings.presets[3]);

    display.setCursor(8, 191);
    display.print(portalActive
      ? "WEB ON: 192.168.4.1"
      : "Hold BACK 1.6s: setup");
  } while (display.nextPage());

  // The e-paper retains its image without power. Shut down panel drive
  // voltages after each refresh; keep controller RAM for partial updates.
  display.powerOff();
  _renderCount++;
}