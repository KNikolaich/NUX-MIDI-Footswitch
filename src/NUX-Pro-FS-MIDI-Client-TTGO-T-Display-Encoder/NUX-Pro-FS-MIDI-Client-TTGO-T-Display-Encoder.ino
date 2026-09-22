/**
 * BLE-MIDI footswitch for NUX MIGHTY PLUG PRO.
 *
 * Target board: TTGO T-Display ESP32 (ST7789 135x240 TFT).
 * Controls: the built-in GPIO35 button selects the next preset; the built-in
 * GPIO0 button sends the currently selected preset.
 * The built-in TFT displays BLE status and the selected preset.
 *
 * Built-in button wiring on TTGO T-Display V1.1:
 *   PRESET UP -> GPIO35
 *   SEND PRESET -> GPIO0
 *
 * GPIO35 is input-only and has no internal pull-up. The TTGO board normally
 * provides the required button circuit; verify the board schematic if using
 * an external switch. GPIO0 is a boot-strap pin, so do not hold its button
 * while resetting or powering the board.
 *
 * Required Arduino libraries:
 * - Arduino BLE-MIDI by lathoub
 * - MIDI Library by FortySevenEffects
 * - TFT_eSPI by Bodmer
 *
 * TFT_eSPI setup:
 * Select the TTGO T-Display setup in TFT_eSPI/User_Setup_Select.h.
 * It is usually named Setup25_TTGO_T_Display.h and configures the
 * ST7789 pins used by the original TTGO T-Display.

Что он делает:

подключается к MIGHTY PLUG PRO по BLE-MIDI;
использует встроенный цветной ST7789-дисплей TTGO T-Display;
кнопкой GPIO35 переключает 7 presets вверх;
кнопкой GPIO0 отправляет выбранный preset в NUX;
синхронизирует номер preset, если он изменён непосредственно на MIGHTY PLUG PRO;
оставляет UART для Serial Monitor на скорости 115200.
Кнопки уже установлены на плате TTGO T-Display V1.1:

PRESET UP > GPIO35
SEND PRESET > GPIO0

GPIO0 нельзя удерживать в LOW во время сброса или включения питания.

Для Arduino IDE нужно установить:

espressif не старше версии              2.0.17
Arduino BLE-MIDI от lathoub;          v 1.4.2...3
MIDI Library от FortySevenEffects;
TFT_eSPI от Bodmer.
В настройках TFT_eSPI нужно выбрать конфигурацию:

Setup25_TTGO_T_Display.h

Обычно это делается в файле:

TFT_eSPI/User_Setup_Select.h

 */

#include <Arduino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>
#include <TFT_eSPI.h>

// #define MIDI_DEVICE_NAME "MIGHTY PLUG PRO"
#define MIDI_DEVICE_NAME "cb:4e:fd:a3:6c:1b"
#define MAX_EFFECT_COUNT 7

#define PIN_PRESET_UP 35
#define PIN_SEND_PRESET 0

TFT_eSPI tft = TFT_eSPI();
BLEMIDI_CREATE_INSTANCE(MIDI_DEVICE_NAME, MIDI)

bool isConnected = false;
bool requestInitialPreset = false;
byte currentEffect = 0;

unsigned long lastBleStatusAt = 0;
unsigned long lastPresetUpChangeAt = 0;
unsigned long lastSendPresetChangeAt = 0;
bool lastPresetUpReading = HIGH;
bool lastSendPresetReading = HIGH;
bool presetUpState = HIGH;
bool sendPresetState = HIGH;

void drawHeader(const char *status)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("NUX MIDI FOOTSWITCH", 120, 8, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(status, 120, 34, 2);
}

void showStatus(const char *status)
{
  drawHeader(status);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("MIGHTY PLUG PRO", 120, 72, 2);
}

void showEffect()
{
  drawHeader(isConnected ? "CONNECTED" : "DISCONNECTED");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("PRESET", 120, 60, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawNumber(currentEffect + 1, 120, 82, 6);
}

void sendCurrentEffect()
{
  // The MIGHTY PLUG PRO beta protocol uses CC 49 for preset switching.
  if (!isConnected) {
    Serial.println("Cannot send preset: BLE-MIDI is not connected");
    showEffect();
    return;
  }

  Serial.print("Sending preset: ");
  Serial.println(currentEffect + 1);
  MIDI.sendControlChange(49, currentEffect, 1);
  showEffect();
}

void selectNextEffect()
{
  currentEffect = (currentEffect + 1) % MAX_EFFECT_COUNT;
  Serial.print("Selected preset: ");
  Serial.println(currentEffect + 1);
  showEffect();
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
  Serial.println();
  Serial.println("NUX MIDI footswitch starting");
  Serial.println("Buttons: PRESET UP=GPIO35, SEND PRESET=GPIO0");

  // GPIO35 has no internal pull-up. The TTGO button circuit normally
  // provides the required bias; use an external pull-up for added switches.
  pinMode(PIN_PRESET_UP, INPUT);
  pinMode(PIN_SEND_PRESET, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.setTextFont(2);
  showStatus("SEARCHING...");

  MIDI.begin(MIDI_CHANNEL_OMNI);

  BLEMIDI.setHandleConnected([]() {
    isConnected = true;
    requestInitialPreset = true;
    Serial.println("BLE-MIDI connected to MIGHTY PLUG PRO");
    showStatus("CONNECTED");
  });

  BLEMIDI.setHandleDisconnected([]() {
    isConnected = false;
    requestInitialPreset = false;
    Serial.println("BLE-MIDI disconnected; scanning");
    showStatus("SEARCHING...");
  });

  // Synchronize the display when the NUX sends its current preset.
  MIDI.setHandleControlChange([](byte channel, byte control, byte value) {
    if (control == 49 && value < MAX_EFFECT_COUNT) {
      currentEffect = value;
      Serial.print("Preset received: ");
      Serial.println(currentEffect + 1);
      showEffect();
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
  readButtons();

  if (!isConnected) {
    if (millis() - lastBleStatusAt >= 2000) {
      lastBleStatusAt = millis();
      Serial.println("BLE: searching for MIGHTY PLUG PRO...");
    }
    delay(5);
    return;
  }

  if (millis() - lastBleStatusAt >= 5000) {
    lastBleStatusAt = millis();
    Serial.println("BLE: connected");
  }

  if (requestInitialPreset) {
    // Let BLE-MIDI finish settling before asking the device for its state.
    delay(250);
    MIDI.sendControlChange(49, 0, 1);
    requestInitialPreset = false;
    showEffect();
  }

  delay(1);
}