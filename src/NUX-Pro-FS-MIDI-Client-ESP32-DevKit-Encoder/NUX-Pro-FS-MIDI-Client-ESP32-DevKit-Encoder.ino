/**
 * Minimal BLE-MIDI footswitch for NUX MIGHTY PLUG PRO.
 *
 * Target board: ESP32 DevKit / ESP32-WROOM-32 compatible board.
 * This version has no display. Status and preset changes are printed to
 * Serial Monitor at 115200 baud.
 *
 * Button wiring:
 *   PRESET UP button -> GPIO35
 *   SEND PRESET button -> GPIO0
 *
 * GPIO35 is input-only and has no internal pull-up. Use an external 10 kOhm
 * pull-up from GPIO35 to 3.3 V and connect the button between GPIO35 and GND.
 * GPIO0 uses the ESP32 internal pull-up; connect its button between GPIO0 and
 * GND. GPIO0 is a boot-strap pin, so do not hold this button while resetting
 * or powering the board, or the ESP32 may enter download mode.
 *
 * Required Arduino libraries:
 * - Arduino BLE-MIDI by lathoub
 * - MIDI Library by FortySevenEffects
 * - NimBLE-Arduino 1.4.2 for the legacy BLE-MIDI version
 */

#include <Arduino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>

#define MIDI_DEVICE_NAME "MIGHTY PLUG PRO"
#define MAX_EFFECT_COUNT 7

#define PIN_PRESET_UP 35
#define PIN_SEND_PRESET 0

BLEMIDI_CREATE_INSTANCE(MIDI_DEVICE_NAME, MIDI)

bool isConnected = false;
bool requestInitialPreset = false;
byte currentEffect = 0;

unsigned long lastPresetUpChangeAt = 0;
unsigned long lastSendPresetChangeAt = 0;
bool lastPresetUpReading = HIGH;
bool lastSendPresetReading = HIGH;
bool presetUpState = HIGH;
bool sendPresetState = HIGH;

void printPreset()
{
  Serial.print("Current preset: ");
  Serial.println(currentEffect + 1);
}

void sendCurrentEffect()
{
  // MIGHTY PLUG PRO uses CC 49 for preset switching.
  if (!isConnected) {
    Serial.println("Cannot send preset: BLE-MIDI is not connected");
    return;
  }

  Serial.print("Sending preset: ");
  Serial.println(currentEffect + 1);
  MIDI.sendControlChange(49, currentEffect, 1);
}

void selectNextEffect()
{
  currentEffect = (currentEffect + 1) % MAX_EFFECT_COUNT;
  Serial.print("Selected preset: ");
  Serial.println(currentEffect + 1);
}

bool buttonWasPressed(
  uint8_t pin,
  bool &lastReading,
  bool &stableState,
  unsigned long &lastChangeAt
)
{
  const bool reading = digitalRead(pin);

  if (reading != lastReading) {
    lastChangeAt = millis();
    lastReading = reading;
  }

  if ((millis() - lastChangeAt) >= 30 && reading != stableState) {
    stableState = reading;
    return stableState == LOW;
  }

  return false;
}

void readButtons()
{
  if (buttonWasPressed(
        PIN_PRESET_UP,
        lastPresetUpReading,
        presetUpState,
        lastPresetUpChangeAt
      )) {
    selectNextEffect();
  }

  if (buttonWasPressed(
        PIN_SEND_PRESET,
        lastSendPresetReading,
        sendPresetState,
        lastSendPresetChangeAt
      )) {
    sendCurrentEffect();
  }
}

void midiReadTask(void *parameter)
{
  for (;;) {
    MIDI.read();
    vTaskDelay(1);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("NUX MIDI footswitch starting");
  Serial.println("Board: ESP32 DevKit / ESP32-WROOM-32");
  Serial.println("Buttons: PRESET UP=GPIO35, SEND PRESET=GPIO0");

  // GPIO35 has no internal pull-up. Add an external 10 kOhm pull-up to 3.3 V.
  pinMode(PIN_PRESET_UP, INPUT);
  pinMode(PIN_SEND_PRESET, INPUT_PULLUP);

  MIDI.begin(MIDI_CHANNEL_OMNI);

  BLEMIDI.setHandleConnected([]() {
    isConnected = true;
    requestInitialPreset = true;
    Serial.println("BLE-MIDI connected to MIGHTY PLUG PRO");
  });

  BLEMIDI.setHandleDisconnected([]() {
    isConnected = false;
    requestInitialPreset = false;
    Serial.println("BLE-MIDI disconnected; scanning");
  });

  // Synchronize the current preset when it changes on the NUX device.
  MIDI.setHandleControlChange([](byte channel, byte control, byte value) {
    if (control == 49 && value < MAX_EFFECT_COUNT) {
      currentEffect = value;
      Serial.print("Preset received from NUX: ");
      printPreset();
    }
  });

  xTaskCreatePinnedToCore(
    midiReadTask,
    "MIDI-READ",
    3000,
    NULL,
    1,
    NULL,
    1
  );
}

void loop()
{
  if (!isConnected) {
    readButtons();
    delay(5);
    return;
  }

  if (requestInitialPreset) {
    // Let BLE-MIDI finish settling before asking the device for its state.
    delay(250);
    MIDI.sendControlChange(49, 0, 1);
    requestInitialPreset = false;
    printPreset();
  }

  readButtons();
  delay(1);
}