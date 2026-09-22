#include "MidiScanner.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>

namespace
{
  portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
  String eventLog;
  String captureLog;
  volatile bool connected = false;
  volatile bool captureEnabled = false;
  bool readTaskStarted = false;

  String loadMidiTargetBeforeBleStarts()
  {
    const char *defaultTarget = "cb:4e:fd:a3:6c:1b";
    const esp_err_t flashStatus = nvs_flash_init();
    if (flashStatus != ESP_OK)
      return defaultTarget;

    nvs_handle_t handle;
    if (nvs_open("nuxscanner", NVS_READONLY, &handle) != ESP_OK)
      return defaultTarget;

    size_t length = 0;
    if (nvs_get_str(handle, "midi", nullptr, &length) != ESP_OK ||
        length < 2 ||
        length > 23) {
      nvs_close(handle);
      return defaultTarget;
    }

    char storedTarget[23] = {};
    const esp_err_t readStatus =
      nvs_get_str(handle, "midi", storedTarget, &length);
    nvs_close(handle);

    if (readStatus != ESP_OK || storedTarget[0] == '\0')
      return defaultTarget;
    return String(storedTarget);
  }

  String configuredTarget = loadMidiTargetBeforeBleStarts();

  void appendLine(String &target, const String &line)
  {
    portENTER_CRITICAL(&logMux);
    target += line;
    target += '\n';

    const size_t maxLength = 14000;
    if (target.length() > maxLength)
      target.remove(0, target.length() - maxLength);
    portEXIT_CRITICAL(&logMux);
  }

  void appendIncoming(const String &line)
  {
    if (!captureEnabled)
      return;

    appendLine(captureLog, line);
    Serial.print("[MIDI RX] ");
    Serial.println(line);
  }

  void handleConnected()
  {
    connected = true;
    midiScannerLog("BLE-MIDI connected");
  }

  void handleDisconnected()
  {
    connected = false;
    midiScannerLog("BLE-MIDI disconnected; scanning");
  }

  void handleControlChange(byte channel, byte control, byte value)
  {
    String line = "CC channel=" + String(channel) +
                  " control=" + String(control) +
                  " value=" + String(value);
    appendIncoming(line);
  }

  void handleProgramChange(byte channel, byte program)
  {
    String line = "PC channel=" + String(channel) +
                  " program=" + String(program);
    appendIncoming(line);
  }

}

// The target is loaded before this global transport is constructed. The
// transport copies the target into its own buffer during construction.
BLEMIDI_CREATE_INSTANCE(configuredTarget.c_str(), MIDI)

static void midiReadTask(void *parameter)
{
  (void)parameter;
  for (;;) {
    MIDI.read();
    vTaskDelay(1);
  }
}

void midiScannerBegin()
{
  MIDI.begin(MIDI_CHANNEL_OMNI);

  BLEMIDI.setHandleConnected(handleConnected);
  BLEMIDI.setHandleDisconnected(handleDisconnected);
  MIDI.setHandleControlChange(handleControlChange);
  MIDI.setHandleProgramChange(handleProgramChange);

  if (!readTaskStarted) {
    xTaskCreatePinnedToCore(
      midiReadTask,
      "MIDI-READ",
      3000,
      nullptr,
      1,
      nullptr,
      1
    );
    readTaskStarted = true;
  }

  midiScannerLog("BLE target: " + configuredTarget);
  midiScannerLog(
    "Listening for CC and Program Change; SysEx is intentionally ignored"
  );
}

bool midiScannerIsConnected()
{
  return connected;
}

const String &midiScannerTarget()
{
  return configuredTarget;
}

bool midiScannerSendControlChange(
  uint8_t control,
  uint8_t value,
  uint8_t channel
)
{
  if (channel < 1 || channel > 16 || control > 127 || value > 127)
    return false;

  if (!connected) {
    midiScannerLog(
      "CC skipped: BLE is not connected (control=" +
      String(control) + ", value=" + String(value) +
      ", channel=" + String(channel) + ")"
    );
    return false;
  }

  MIDI.sendControlChange(control, value, channel);
  midiScannerLog(
    "CC sent: control=" + String(control) +
    " value=" + String(value) +
    " channel=" + String(channel)
  );
  return true;
}

void midiScannerSetCapture(bool enabled)
{
  captureEnabled = enabled;
  midiScannerLog(enabled ? "MIDI capture started" : "MIDI capture stopped");
}

bool midiScannerCaptureEnabled()
{
  return captureEnabled;
}

void midiScannerClearCapture()
{
  portENTER_CRITICAL(&logMux);
  captureLog = "";
  portEXIT_CRITICAL(&logMux);
}

String midiScannerCaptureLog()
{
  portENTER_CRITICAL(&logMux);
  String result = captureLog;
  portEXIT_CRITICAL(&logMux);
  return result;
}

void midiScannerLog(const String &line)
{
  appendLine(eventLog, line);
  Serial.println("[MIDI] " + line);
}

void midiScannerClearEventLog()
{
  portENTER_CRITICAL(&logMux);
  eventLog = "";
  portEXIT_CRITICAL(&logMux);
}

String midiScannerEventLog()
{
  portENTER_CRITICAL(&logMux);
  String result = eventLog;
  portEXIT_CRITICAL(&logMux);
  return result;
}