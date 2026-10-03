#pragma once

#include <Arduino.h>

class NuxMidiClient
{
public:
  bool begin();
  bool connected() const;
  uint8_t currentPreset() const;

  bool sendPreset(uint8_t preset);
  void requestCurrentPreset();
  bool consumeConnectionChange(bool &isConnected);
  bool consumePresetChange(uint8_t &preset);

private:
  volatile bool _connected = false;
  volatile bool _connectionChanged = false;
  volatile bool _presetChanged = false;
  volatile uint8_t _currentPreset = 1;
};