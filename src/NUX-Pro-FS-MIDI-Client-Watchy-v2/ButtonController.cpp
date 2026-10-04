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

  ButtonEvent singleTapEvent(uint8_t buttonIndex)
  {
    switch (buttonIndex) {
      case 0: return ButtonEvent::Menu;
      case 1: return ButtonEvent::Back;
      case 2: return ButtonEvent::Up;
      case 3: return ButtonEvent::Down;
      default: return ButtonEvent::None;
    }
  }

  ButtonEvent doubleTapEvent(uint8_t buttonIndex)
  {
    switch (buttonIndex) {
      case 0: return ButtonEvent::CycleMenuPreset;
      case 1: return ButtonEvent::CycleBackPreset;
      case 2: return ButtonEvent::CycleUpPreset;
      case 3: return ButtonEvent::CycleDownPreset;
      default: return ButtonEvent::None;
    }
  }
}

void ButtonController::begin(bool ignoreButtonsHeldAtBoot)
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
    _states[i].longPressHandled = ignoreButtonsHeldAtBoot && pressed;
    _states[i].pendingTap = false;
    _states[i].doubleTapInProgress = false;
    _states[i].pendingTapAt = 0;
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

    // Let a second press finish debouncing if its raw edge started inside the
    // double-tap window, even when debounce completes just after the deadline.
    const bool secondPressStartedInWindow =
      state.pendingTap &&
      rawPressed &&
      state.changedAt - state.pendingTapAt <
        WatchyConfig::BUTTON_DOUBLE_TAP_MS;
    if (state.pendingTap &&
        !state.stablePressed &&
        now - state.pendingTapAt >= WatchyConfig::BUTTON_DOUBLE_TAP_MS &&
        !secondPressStartedInWindow) {
      state.pendingTap = false;
      return singleTapEvent(i);
    }

    if (now - state.changedAt >= WatchyConfig::BUTTON_DEBOUNCE_MS &&
        rawPressed != state.stablePressed) {
      state.stablePressed = rawPressed;
      if (rawPressed) {
        state.pressedAt = now;
        state.longPressHandled = false;
        state.doubleTapInProgress =
          state.pendingTap &&
          state.changedAt - state.pendingTapAt <
            WatchyConfig::BUTTON_DOUBLE_TAP_MS;
        if (state.doubleTapInProgress)
          state.pendingTap = false;
      } else if (state.doubleTapInProgress) {
        state.doubleTapInProgress = false;
        state.longPressHandled = true;
        return doubleTapEvent(i);
      } else if (!state.longPressHandled) {
        state.pendingTap = true;
        state.pendingTapAt = now;
      } else {
        state.pendingTap = false;
      }
    }

    if (state.stablePressed &&
        !state.longPressHandled &&
        !state.doubleTapInProgress) {
      if (i == 1 &&
          now - state.pressedAt >= WatchyConfig::PORTAL_HOLD_MS) {
        state.longPressHandled = true;
        state.pendingTap = false;
        return ButtonEvent::PortalToggle;
      }
      if (i == 0 &&
          now - state.pressedAt >= WatchyConfig::DEEP_SLEEP_HOLD_MS) {
        state.longPressHandled = true;
        state.pendingTap = false;
        return ButtonEvent::EnterDeepSleep;
      }
    }
  }

  return ButtonEvent::None;
}