#pragma once

#include <Arduino.h>

enum class ButtonEvent : uint8_t
{
  None,
  Menu,
  Back,
  Up,
  Down,
  PortalToggle
};

class ButtonController
{
public:
  void begin();
  ButtonEvent poll();

private:
  struct ButtonState
  {
    bool lastRawPressed = false;
    bool stablePressed = false;
    bool longPressHandled = false;
    uint32_t changedAt = 0;
    uint32_t pressedAt = 0;
  };

  ButtonState _states[4];
};