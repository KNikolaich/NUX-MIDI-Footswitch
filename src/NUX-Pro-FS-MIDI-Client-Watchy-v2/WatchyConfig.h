#pragma once

#include <Arduino.h>

namespace WatchyConfig
{
  constexpr uint8_t BUTTON_COUNT = 4;
  constexpr uint8_t PRESET_COUNT = 7;

  // Confirmed against the Watchy v2.0 board definition and schematic.
  constexpr uint8_t MENU_BTN_PIN = 26;
  constexpr uint8_t BACK_BTN_PIN = 25;
  constexpr uint8_t UP_BTN_PIN = 35;
  constexpr uint8_t DOWN_BTN_PIN = 4;

  constexpr uint8_t DISPLAY_CS_PIN = 5;
  constexpr uint8_t DISPLAY_DC_PIN = 10;
  constexpr uint8_t DISPLAY_RESET_PIN = 9;
  constexpr uint8_t DISPLAY_BUSY_PIN = 19;
  constexpr int8_t DISPLAY_MISO_PIN = -1;
  constexpr uint8_t DISPLAY_MOSI_PIN = 23;
  constexpr uint8_t DISPLAY_SCK_PIN = 18;

  constexpr uint8_t I2C_SDA_PIN = 21;
  constexpr uint8_t I2C_SCL_PIN = 22;
  constexpr uint8_t VIBRATION_MOTOR_PIN = 13;

  // Watchy v2.0's four buttons are pulled low on the board and read HIGH
  // while pressed. GPIO35 has no internal pull-up/down.
  constexpr uint8_t BUTTON_PRESSED_LEVEL = HIGH;
  constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
  constexpr uint32_t PORTAL_HOLD_MS = 1600;
  constexpr uint32_t PORTAL_IDLE_TIMEOUT_MS = 180000;
  constexpr uint32_t FULL_DISPLAY_REFRESH_EVERY = 20;

  constexpr char DEFAULT_BLE_TARGET[] = "cb:4e:fd:a3:6c:1b";
  constexpr char AP_SSID[] = "NUX-Watchy-Setup";
  constexpr char AP_PASSWORD[] = "nux12345";
  constexpr char WEB_USERNAME[] = "admin";
  constexpr char WEB_PASSWORD[] = "nux12345";
}