/**
 * BLE-MIDI footswitch for NUX MIGHTY PLUG PRO.
 *
 * Target board: TTGO T-Display ESP32 (ST7789 135x240 TFT).
 * Controls: mechanical rotary encoder; pressing the encoder selects preset 1.
 * The built-in TFT displays BLE status and the selected preset.
 *
 * Encoder wiring:
 *   CLK -> GPIO25
 *   DT  -> GPIO26
 *   SW  -> GPIO27
 *   VCC -> 3.3V
 *   GND -> GND
 *
 * The encoder must be powered from 3.3 V. ESP32 GPIO pins are not 5 V tolerant.
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
вращением энкодера переключает 7 presets;
нажатием на энкодер возвращается к preset 1;
синхронизирует номер preset, если он изменён непосредственно на MIGHTY PLUG PRO;
оставляет UART для Serial Monitor на скорости 115200.
Распиновка энкодера:

CLK > GPIO25
DT  > GPIO26
SW  > GPIO27
VCC > 3.3V
GND > GND

Питание энкодера обязательно от 3.3 В. На GPIO ESP32 нельзя подавать 5 В.

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

#define MIDI_DEVICE_NAME "MIGHTY PLUG PRO"
#define MAX_EFFECT_COUNT 7

// These GPIOs are available on the original TTGO T-Display and do not drive
// the built-in TFT. Do not use 0, 4, 5, 16, 18, 19 or 23 for the encoder.
#define PIN_ENCODER_CLK 25
#define PIN_ENCODER_DT 26
#define PIN_ENCODER_SW 27

TFT_eSPI tft = TFT_eSPI();
BLEMIDI_CREATE_INSTANCE(MIDI_DEVICE_NAME, MIDI)

volatile int8_t encoderMovement = 0;
volatile uint8_t previousEncoderState = 0;

bool isConnected = false;
bool requestInitialPreset = false;
byte currentEffect = 0;

unsigned long lastButtonChangeAt = 0;
bool lastButtonReading = HIGH;
bool buttonState = HIGH;

void IRAM_ATTR handleEncoder()
{
  const uint8_t state =
    (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);

  // Gray-code transition table. Invalid/bouncing transitions add zero.
  static const int8_t transitions[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  encoderMovement += transitions[(previousEncoderState << 2) | state];
  previousEncoderState = state;
}

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
  Serial.print("Sending preset: ");
  Serial.println(currentEffect + 1);
  MIDI.sendControlChange(49, currentEffect, 1);
  showEffect();
}

void setEffect(int effect)
{
  if (effect < 0)
    effect = MAX_EFFECT_COUNT - 1;
  else if (effect >= MAX_EFFECT_COUNT)
    effect = 0;

  currentEffect = effect;
  sendCurrentEffect();
}

void readEncoder()
{
  int8_t movement;

  noInterrupts();
  movement = encoderMovement;
  // Most mechanical encoders produce four valid transitions per detent.
  if (movement >= 4)
    encoderMovement -= 4;
  else if (movement <= -4)
    encoderMovement += 4;
  interrupts();

  if (movement >= 4)
    setEffect(currentEffect + 1);
  else if (movement <= -4)
    setEffect(currentEffect - 1);
}

void readEncoderButton()
{
  const bool reading = digitalRead(PIN_ENCODER_SW);

  if (reading != lastButtonReading) {
    lastButtonChangeAt = millis();
    lastButtonReading = reading;
  }

  if ((millis() - lastButtonChangeAt) >= 30 && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW)
      setEffect(0);
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

  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);

  previousEncoderState =
    (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_DT), handleEncoder, CHANGE);

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
  if (!isConnected) {
    delay(5);
    return;
  }

  if (requestInitialPreset) {
    // Let BLE-MIDI finish settling before asking the device for its state.
    delay(250);
    MIDI.sendControlChange(49, 0, 1);
    requestInitialPreset = false;
    showEffect();
  }

  readEncoder();
  readEncoderButton();
  delay(1);
}