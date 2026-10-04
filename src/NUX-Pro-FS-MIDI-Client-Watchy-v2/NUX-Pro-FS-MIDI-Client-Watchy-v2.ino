/*
 * NUX MIGHTY PLUG PRO BLE-MIDI footswitch on Watchy v2.0.
 *
 * Four physical buttons recall their assigned presets and support configurable
 * per-button double-tap actions.
 * Holding BACK for 1.6 s toggles the settings/OTA access point; holding MENU
 * for 2 s enters deep sleep, with any button able to wake the ESP32.
 *
 * Watchy v2.0 button inputs are active HIGH and use the board's external
 * pull-downs. The e-paper panel is driven directly through GxEPD2; the
 * Watchy framework is intentionally not initialized; the sketch manages the
 * display directly and uses ESP32 RTC GPIOs for its own deep-sleep wake.
 */

#include <Arduino.h>
#include <esp_sleep.h>

#include "ButtonController.h"
#include "DeviceSettings.h"
#include "FirmwareVersion.h"
#include "MidiClient.h"
#include "WatchyConfig.h"
#include "WatchyScreen.h"
#include "WebConfig.h"

namespace
{
  DeviceSettingsStore settingsStore;
  ButtonController buttons;
  NuxMidiClient midiClient;
  WatchyScreen screen;
  WebConfig webConfig;

  bool presetRequestPending = false;
  uint32_t presetRequestAt = 0;
  uint32_t lastFullRefreshAt = 0;

  void showCurrentState(bool fullRefresh = false)
  {
    const DeviceSettings &settings = settingsStore.get();
    screen.render(
      settings,
      midiClient.currentPreset(),
      midiClient.connected(),
      webConfig.portalActive(),
      fullRefresh
    );
    if (fullRefresh)
      lastFullRefreshAt = millis();
  }

  void recallButtonPreset(uint8_t buttonIndex, const char *buttonName)
  {
    const DeviceSettings &settings = settingsStore.get();
    const uint8_t preset = settings.presets[buttonIndex];
    Serial.printf("[BUTTON] %s -> preset %u\n", buttonName, preset);
    midiClient.sendPreset(preset);
    showCurrentState();
  }

  void cycleButtonPreset(uint8_t buttonIndex, const char *buttonName)
  {
    const DeviceSettings &settings = settingsStore.get();
    uint8_t presets[WatchyConfig::BUTTON_COUNT];
    DoubleTapAction doubleTapActions[WatchyConfig::BUTTON_COUNT];
    uint8_t doubleTapPresets[WatchyConfig::BUTTON_COUNT];
    for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; ++i) {
      presets[i] = settings.presets[i];
      doubleTapActions[i] = settings.doubleTapActions[i];
      doubleTapPresets[i] = settings.doubleTapPresets[i];
    }

    presets[buttonIndex] =
      (presets[buttonIndex] % WatchyConfig::PRESET_COUNT) + 1;
    if (!settingsStore.save(
          settings.bleTarget, presets, doubleTapActions, doubleTapPresets)) {
      Serial.printf("[SETTINGS] Could not save %s preset assignment\n", buttonName);
      showCurrentState();
      return;
    }

    Serial.printf("[BUTTON] %s double tap -> preset %u\n",
      buttonName, presets[buttonIndex]);
    midiClient.sendPreset(presets[buttonIndex]);
    showCurrentState();
  }

  void handleDoubleTap(uint8_t buttonIndex, const char *buttonName)
  {
    const DeviceSettings &settings = settingsStore.get();
    switch (settings.doubleTapActions[buttonIndex]) {
      case DoubleTapAction::Disabled:
        Serial.printf("[BUTTON] %s double tap is disabled\n", buttonName);
        break;
      case DoubleTapAction::CycleNext:
        cycleButtonPreset(buttonIndex, buttonName);
        break;
      case DoubleTapAction::FixedPreset: {
        const uint8_t preset = settings.doubleTapPresets[buttonIndex];
        Serial.printf("[BUTTON] %s double tap -> fixed preset %u\n",
          buttonName, preset);
        midiClient.sendPreset(preset);
        showCurrentState();
        break;
      }
      default:
        Serial.printf("[BUTTON] %s double tap has an invalid action\n",
          buttonName);
        break;
    }
  }

  bool anyButtonPressed()
  {
    return digitalRead(WatchyConfig::MENU_BTN_PIN) == WatchyConfig::BUTTON_PRESSED_LEVEL ||
           digitalRead(WatchyConfig::BACK_BTN_PIN) == WatchyConfig::BUTTON_PRESSED_LEVEL ||
           digitalRead(WatchyConfig::UP_BTN_PIN) == WatchyConfig::BUTTON_PRESSED_LEVEL ||
           digitalRead(WatchyConfig::DOWN_BTN_PIN) == WatchyConfig::BUTTON_PRESSED_LEVEL;
  }

  void enterDeepSleep()
  {
    Serial.println("[POWER] Releasing buttons before deep sleep");
    if (webConfig.portalActive()) {
      webConfig.togglePortal();
      showCurrentState(true);
    }

    do {
      while (anyButtonPressed())
        delay(10);
      delay(WatchyConfig::BUTTON_DEBOUNCE_MS + 20);
    } while (anyButtonPressed());

    const uint64_t wakeMask =
      (1ULL << WatchyConfig::MENU_BTN_PIN) |
      (1ULL << WatchyConfig::BACK_BTN_PIN) |
      (1ULL << WatchyConfig::UP_BTN_PIN) |
      (1ULL << WatchyConfig::DOWN_BTN_PIN);
    const esp_err_t wakeConfig =
      esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_HIGH);
    if (wakeConfig != ESP_OK) {
      Serial.printf("[POWER] Could not configure button wake: %d\n", wakeConfig);
      return;
    }

    screen.renderSleepScreen();
    Serial.println("[POWER] Entering deep sleep; press any button to wake");
    Serial.flush();
    esp_deep_sleep_start();
  }

  void printStartupGuide()
  {
    Serial.println();
    Serial.println("==============================================");
    Serial.println(" NUX MIDI FOOTSWITCH");
    Serial.printf(" Firmware: v%s | Hardware: Watchy v2.0\n",
      FirmwareVersion::STRING);
    Serial.println(" ESP32-PICO-D4 | 1.54in 200x200 e-paper");
    Serial.println(" Target: NUX MIGHTY PLUG PRO via BLE-MIDI");
    Serial.println("==============================================");
    Serial.println("Short press a button to send its assigned preset.");
    Serial.println("Double tap follows the per-button action in /settings.");
    Serial.println(" MENU GPIO26, BACK GPIO25, UP GPIO35, DOWN GPIO4");
    Serial.println("Hold BACK for 1.6 s to open/close settings and OTA.");
    Serial.println("Hold MENU for 2 s, release to sleep; any button wakes.");
    Serial.println("BLE stays active; WiFi AP is off except in setup mode.");
    Serial.println("RTC, accelerometer, time sync and vibration are unused.");
    Serial.println("Serial Monitor: 115200 baud");
    Serial.println("==============================================");
  }

  void handleButtonEvent(ButtonEvent event)
  {
    switch (event) {
      case ButtonEvent::Menu:
        recallButtonPreset(0, "MENU / GPIO26");
        break;
      case ButtonEvent::Back:
        recallButtonPreset(1, "BACK / GPIO25");
        break;
      case ButtonEvent::Up:
        recallButtonPreset(2, "UP / GPIO35");
        break;
      case ButtonEvent::Down:
        recallButtonPreset(3, "DOWN / GPIO4");
        break;
      case ButtonEvent::DoubleTapMenu:
        handleDoubleTap(0, "MENU / GPIO26");
        break;
      case ButtonEvent::DoubleTapBack:
        handleDoubleTap(1, "BACK / GPIO25");
        break;
      case ButtonEvent::DoubleTapUp:
        handleDoubleTap(2, "UP / GPIO35");
        break;
      case ButtonEvent::DoubleTapDown:
        handleDoubleTap(3, "DOWN / GPIO4");
        break;
      case ButtonEvent::EnterDeepSleep:
        enterDeepSleep();
        break;
      case ButtonEvent::PortalToggle:
        webConfig.togglePortal();
        // Switching the AP screen changes several static fields and must clear
        // the previous contents completely.
        showCurrentState(true);
        break;
      case ButtonEvent::None:
      default:
        break;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  const bool wokeByButton =
    esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
  if (wokeByButton)
    Serial.println("[POWER] Woke from deep sleep via a button");
  printStartupGuide();

  if (!settingsStore.begin())
    Serial.println("[NVS] Could not read settings; defaults are active.");

  const DeviceSettings &settings = settingsStore.get();
  Serial.printf("[BOOT] BLE target: %s\n", settings.bleTarget.c_str());
  Serial.printf("[BOOT] Presets MENU/BACK/UP/DOWN: %u/%u/%u/%u\n",
    settings.presets[0],
    settings.presets[1],
    settings.presets[2],
    settings.presets[3]);

  // Keep the vibration motor transistor disabled. RTC and BMA423 are not
  // initialized; their hardware remains on the board's always-on 3.3 V rail.
  digitalWrite(WatchyConfig::VIBRATION_MOTOR_PIN, LOW);
  pinMode(WatchyConfig::VIBRATION_MOTOR_PIN, OUTPUT);

  buttons.begin(wokeByButton);
  if (!screen.begin())
    Serial.println("[DISPLAY] E-paper initialization failed.");
  screen.render(settings, 1, false, false, true);
  lastFullRefreshAt = millis();

  webConfig.begin(settingsStore);
  if (!midiClient.begin())
    Serial.println("[BLE] MIDI task did not start; check available memory.");
  Serial.println("[BOOT] Ready. Waiting for button presses and BLE connection.");
}

void loop()
{
  const bool portalWasActive = webConfig.portalActive();
  webConfig.loop();
  if (portalWasActive != webConfig.portalActive())
    showCurrentState(true);

  if (webConfig.restartRequested()) {
    delay(150);
    ESP.restart();
  }

  handleButtonEvent(buttons.poll());

  bool connected = false;
  if (midiClient.consumeConnectionChange(connected)) {
    showCurrentState();
    if (connected) {
      presetRequestPending = true;
      presetRequestAt = millis() + 250;
    } else {
      presetRequestPending = false;
    }
  }

  if (presetRequestPending &&
      (int32_t)(millis() - presetRequestAt) >= 0) {
    midiClient.requestCurrentPreset();
    presetRequestPending = false;
  }

  uint8_t receivedPreset = 0;
  if (midiClient.consumePresetChange(receivedPreset)) {
    Serial.printf("[MIDI] NUX reports preset %u\n", receivedPreset);
    showCurrentState();
  }

  if (millis() - lastFullRefreshAt >= WatchyConfig::BATTERY_DISPLAY_REFRESH_MS)
    showCurrentState(true);

  delay(2);
}