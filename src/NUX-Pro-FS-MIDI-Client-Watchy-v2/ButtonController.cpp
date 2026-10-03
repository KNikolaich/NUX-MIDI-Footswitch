#include "ButtonController.h"

#include "WatchyConfig.h"

namespace
{
  constexpr uint8_t BUTTON_PINS[WatchyConfig::BUTTON_COUNT] = {
    WatchyConfig::MENU_BTN_PIN,
    WatchyConfig::BACK_BTN_PIN,
    WatchyConfig::UP_BTN_PIN,
    WatchyConfig::DOWN_BTN_PIN
  };
}

void ButtonController::begin()
{
  const uint32_t now = millis();
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    // Watchy v2 has external pull-downs. GPIO35 has no internal pull resistor.
    pinMode(BUTTON_PINS[i], INPUT);
    const bool pressed =
      digitalRead(BUTTON_PINS[i]) == WatchyConfig::BUTTON_PRESSED_LEVEL;
    _states[i].lastRawPressed = pressed;
    _states[i].stablePressed = pressed;
    _states[i].changedAt = now;
    _states[i].pressedAt = pressed ? now : 0;
  }
}

ButtonEvent ButtonController::poll()
{
  const uint32_t now = millis();

  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; i++) {
    ButtonState &state = _states[i];
    const bool rawPressed =
      digitalRead(BUTTON_PINS[i]) == WatchyConfig::BUTTON_PRESSED_LEVEL;

    if (rawPressed != state.lastRawPressed) {
      state.lastRawPressed = rawPressed;
      state.changedAt = now;
    }

    if (now - state.changedAt >= WatchyConfig::BUTTON_DEBOUNCE_MS &&
        rawPressed != state.stablePressed) {
      state.stablePressed = rawPressed;
      if (rawPressed) {
        state.pressedAt = now;
        state.longPressHandled = false;
        if (i == 0)
          return ButtonEvent::Menu;
        if (i == 2)
          return ButtonEvent::Up;
        if (i == 3)
          return ButtonEvent::Down;
      } else if (i == 1 && !state.longPressHandled) {
        // A short BACK press recalls its configured preset.
        return ButtonEvent::Back;
      }
    }

    if (i == 1 && state.stablePressed && !state.longPressHandled &&
        now - state.pressedAt >= WatchyConfig::PORTAL_HOLD_MS) {
      state.longPressHandled = true;
      return ButtonEvent::PortalToggle;
    }
  }

  return ButtonEvent::None;
}