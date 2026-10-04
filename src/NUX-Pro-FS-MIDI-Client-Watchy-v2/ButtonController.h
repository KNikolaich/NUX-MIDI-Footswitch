#pragma once

#include <Arduino.h>

enum class ButtonEvent : uint8_t
{
  None,
  Menu,
  Back,
  Up,
  Down,
  PortalToggle,
  DoubleTapMenu,
  DoubleTapBack,
  DoubleTapUp,
  DoubleTapDown,
  EnterDeepSleep
};

class ButtonController
{
public:
  void begin(bool ignoreButtonsHeldAtBoot = false);
  ButtonEvent poll();

private:
  struct ButtonState
  {
    bool lastRawPressed = false;
    bool stablePressed = false;
    bool longPressHandled = false;
    bool pendingTap = false;
    bool doubleTapInProgress = false;
    uint32_t changedAt = 0;
    uint32_t pressedAt = 0;
    uint32_t pendingTapAt = 0;
  };

  ButtonState _states[4];
};