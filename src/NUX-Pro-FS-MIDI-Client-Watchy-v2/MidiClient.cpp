#include "MidiClient.h"

#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>
#include <MIDI.h>

#include "DeviceSettings.h"
#include "WatchyConfig.h"

namespace
{
  String configuredBleTarget = DeviceSettingsStore::loadBleTargetEarly();
  NuxMidiClient *activeMidiClient = nullptr;
}

BLEMIDI_CREATE_INSTANCE(configuredBleTarget.c_str(), MIDI)

namespace
{
  void midiReadTask(void *)
  {
    for (;;) {
      MIDI.read();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

bool NuxMidiClient::begin()
{
  activeMidiClient = this;
  MIDI.begin(MIDI_CHANNEL_OMNI);

  BLEMIDI.setHandleConnected([]() {
    if (activeMidiClient == nullptr)
      return;
    activeMidiClient->_connected = true;
    activeMidiClient->_connectionChanged = true;
    Serial.println("[BLE] Connected to NUX MIDI target");
  });

  BLEMIDI.setHandleDisconnected([]() {
    if (activeMidiClient == nullptr)
      return;
    activeMidiClient->_connected = false;
    activeMidiClient->_connectionChanged = true;
    Serial.println("[BLE] Disconnected; searching again");
  });

  MIDI.setHandleProgramChange([](byte, byte program) {
    if (activeMidiClient == nullptr ||
        program >= WatchyConfig::PRESET_COUNT)
      return;
    activeMidiClient->_currentPreset = program + 1;
    activeMidiClient->_presetChanged = true;
  });

  MIDI.setHandleSystemExclusive([](byte *data, unsigned size) {
    if (activeMidiClient == nullptr)
      return;

    unsigned start = 0;
    while (start < size && start < 4 && data[start] != 0xF0)
      start++;

    if (size >= start + 9 &&
        data[start] == 0xF0 &&
        data[start + 1] == 0x43 &&
        data[start + 2] == 0x58 &&
        data[start + 3] == 0x70 &&
        data[start + 4] == 0x0C &&
        data[start + 5] == 0x03 &&
        data[start + 7] == 0x32 &&
        data[start + 8] == 0xF7 &&
        data[start + 6] < WatchyConfig::PRESET_COUNT) {
      activeMidiClient->_currentPreset = data[start + 6] + 1;
      activeMidiClient->_presetChanged = true;
    }
  });

  const BaseType_t result = xTaskCreatePinnedToCore(
    midiReadTask,
    "MIDI-READ",
    3000,
    nullptr,
    1,
    nullptr,
    1
  );

  if (result != pdPASS) {
    Serial.println("[BLE] Could not create MIDI read task");
    return false;
  }
  return true;
}

bool NuxMidiClient::connected() const
{
  return _connected;
}

uint8_t NuxMidiClient::currentPreset() const
{
  return _currentPreset;
}

bool NuxMidiClient::sendPreset(uint8_t preset)
{
  if (preset < 1 || preset > WatchyConfig::PRESET_COUNT)
    return false;

  _currentPreset = preset;
  if (!_connected) {
    Serial.println("[MIDI] Preset selected locally; BLE is not connected");
    return false;
  }

  MIDI.sendProgramChange(preset - 1, 1);
  Serial.printf("[MIDI] Sent Program Change: preset %u, channel 1\n", preset);
  return true;
}

void NuxMidiClient::requestCurrentPreset()
{
  if (!_connected)
    return;

  // NUX MIGHTY PLUG PRO private SysEx request for the current preset.
  const byte request[] = {0xF0, 0x43, 0x58, 0x70, 0x0C, 0x02, 0xF7};
  MIDI.sendSysEx(sizeof(request), request, true);
  Serial.println("[MIDI] Requested current preset from NUX");
}

bool NuxMidiClient::consumeConnectionChange(bool &isConnected)
{
  if (!_connectionChanged)
    return false;
  _connectionChanged = false;
  isConnected = _connected;
  return true;
}

bool NuxMidiClient::consumePresetChange(uint8_t &preset)
{
  if (!_presetChanged)
    return false;
  _presetChanged = false;
  preset = _currentPreset;
  return true;
}