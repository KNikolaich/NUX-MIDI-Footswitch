/**
 * BLE-MIDI footswitch for NUX MIGHTY PLUG PRO.
 *
 * Target board: AI-Thinker ESP32-CAM (camera and microSD are not used).
 * Controls: mechanical rotary encoder; press returns to preset 1.
 * Display: SSD1306 128x32 I2C OLED, normally at address 0x3c.
 *
 * IMPORTANT:
 * - Power the encoder and OLED from 3.3 V. ESP32 GPIO pins are not 5 V tolerant.
 * - GPIO1/GPIO3 remain available for Serial Monitor at 115200 baud.
 * - GPIO2 is a bootstrapping pin. Disconnect the OLED while uploading if the
 *   board does not enter download mode; reconnect it after flashing.
 * - Do not initialize the camera or microSD while using this pin assignment.
 *
 * Required Arduino libraries:
 * - Arduino BLE-MIDI by lathoub
 * - MIDI Library by FortySevenEffects
 * - ESP8266 and ESP32 OLED driver for SSD1306 displays by ThingPulse
 */

#include <Arduino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>
#include "SSD1306Wire.h"

#define MIDI_DEVICE_NAME "MIGHTY PLUG PRO"
#define MAX_EFFECT_COUNT 7

// AI-Thinker ESP32-CAM pin assignment (camera and microSD must be unused).
#define PIN_ENCODER_CLK 13
#define PIN_ENCODER_DT 14
#define PIN_ENCODER_SW 15
#define OLED_SDA 2
#define OLED_SCL 4
#define OLED_ADDRESS 0x3c

SSD1306Wire display(
  OLED_ADDRESS,
  OLED_SDA,
  OLED_SCL,
  GEOMETRY_128_32
);

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

void showStatus(const String &line)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 7, line);
  display.display();
}

void showEffect()
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 2, "PRESET " + String(currentEffect + 1));
  display.display();
}

void sendCurrentEffect()
{
  // MIGHTY PLUG PRO switches presets with MIDI Program Change.
  Serial.print("Sending preset: ");
  Serial.println(currentEffect + 1);
  MIDI.sendProgramChange(currentEffect, 1);
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

  display.init();
  display.setContrast(255);
  showStatus("NUX: SEARCH");

  MIDI.begin(MIDI_CHANNEL_OMNI);

  BLEMIDI.setHandleConnected([]() {
    isConnected = true;
    requestInitialPreset = true;
    Serial.println("BLE-MIDI connected to MIGHTY PLUG PRO");
    showStatus("NUX: CONNECTED");
  });

  BLEMIDI.setHandleDisconnected([]() {
    isConnected = false;
    requestInitialPreset = false;
    Serial.println("BLE-MIDI disconnected; scanning");
    showStatus("NUX: SEARCH");
  });

  MIDI.setHandleProgramChange([](byte channel, byte program) {
    if (program < MAX_EFFECT_COUNT) {
      currentEffect = program;
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