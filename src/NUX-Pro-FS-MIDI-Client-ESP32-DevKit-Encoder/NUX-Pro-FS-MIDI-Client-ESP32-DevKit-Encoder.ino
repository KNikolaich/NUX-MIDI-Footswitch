/**
 * Minimal BLE-MIDI footswitch for NUX MIGHTY PLUG PRO.
 *
 * Target board: ESP32 DevKit / ESP32-WROOM-32 compatible board.
 * This version has no display. Status and preset changes are printed to
 * Serial Monitor at 115200 baud.
 *
 * Mechanical encoder wiring:
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
 * - NimBLE-Arduino 1.4.2 for the legacy BLE-MIDI version
 */

#include <Arduino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>

#define MIDI_DEVICE_NAME "MIGHTY PLUG PRO"
#define MAX_EFFECT_COUNT 7

#define PIN_ENCODER_CLK 25
#define PIN_ENCODER_DT 26
#define PIN_ENCODER_SW 27

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

void printPreset()
{
  Serial.print("Current preset: ");
  Serial.println(currentEffect + 1);
}

void sendCurrentEffect()
{
  // MIGHTY PLUG PRO uses CC 49 for preset switching.
  Serial.print("Sending preset: ");
  Serial.println(currentEffect + 1);
  MIDI.sendControlChange(49, currentEffect, 1);
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
    if (buttonState == LOW) {
      Serial.println("Encoder pressed: selecting preset 1");
      setEffect(0);
    }
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
  Serial.println("Encoder: CLK=25, DT=26, SW=27");

  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);

  previousEncoderState =
    (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_CLK), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_DT), handleEncoder, CHANGE);

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

  readEncoder();
  readEncoderButton();
  delay(1);
}