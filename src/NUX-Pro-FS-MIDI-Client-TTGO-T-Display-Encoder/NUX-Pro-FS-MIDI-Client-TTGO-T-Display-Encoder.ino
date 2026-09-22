/**
 * BLE-MIDI footswitch for NUX MIGHTY PLUG PRO.
 *
 * Target board: TTGO T-Display ESP32 (ST7789 135x240 TFT).
 * Controls: the built-in GPIO35 and GPIO0 buttons recall two quick presets.
 * A rotary encoder selects any preset and its push button sends the selection.
 * The built-in TFT displays BLE status and the selected preset.
 *
 * Built-in button wiring on TTGO T-Display V1.1:
 *   QUICK PRESET 1 -> GPIO35
 *   QUICK PRESET 2 -> GPIO0
 *   BATTERY ADC -> GPIO34 (measurement divider enabled by GPIO14)
 *
 * External encoder wiring:
 *   CLK -> GPIO25
 *   DT  -> GPIO26
 *   SW  -> GPIO27
 *   VCC -> 3.3V
 *   GND -> GND
 *
 * GPIO35 is input-only and has no internal pull-up. The TTGO board normally
 * provides the required button circuit; verify the board schematic if using
 * an external switch. GPIO0 is a boot-strap pin, so do not hold its button
 * while resetting or powering the board.
 * The encoder must be powered from 3.3 V because ESP32 GPIO pins are not 5 V
 * tolerant.
 *
 * Battery percentage is an estimate based on the measured Li-ion voltage:
 * 3.30 V is treated as empty and 4.20 V as full.
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
кнопками GPIO35 и GPIO0 мгновенно выбирает preset 1 и preset 5;
encoder выбирает любой из 7 presets по кругу;
кнопка encoder отправляет выбранный preset в NUX;
синхронизирует номер preset, если он изменён непосредственно на MIGHTY PLUG PRO;
оставляет UART для Serial Monitor на скорости 115200.
Кнопки уже установлены на плате TTGO T-Display V1.1:

QUICK PRESET 1 > GPIO35
QUICK PRESET 2 > GPIO0

Encoder:
CLK > GPIO25
DT  > GPIO26
SW  > GPIO27

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
#define QUICK_PRESET_A 1
#define QUICK_PRESET_B 5
#define PRESET_SYNC_TIMEOUT_MS 2000UL
#define BATTERY_REFRESH_INTERVAL_MS 5000UL
#define PIN_BATTERY_ADC 34
#define PIN_BATTERY_ENABLE 14
#define BATTERY_DIVIDER_RATIO 2.0f
#define BATTERY_EMPTY_VOLTAGE 3.30f
#define BATTERY_FULL_VOLTAGE 4.20f

#define PIN_QUICK_PRESET_A 35
#define PIN_QUICK_PRESET_B 0
#define PIN_ENCODER_CLK 25
#define PIN_ENCODER_DT 26
#define PIN_ENCODER_SW 27

TFT_eSPI tft = TFT_eSPI();
BLEMIDI_CREATE_INSTANCE(MIDI_DEVICE_NAME, MIDI)

volatile int8_t encoderMovement = 0;
volatile uint8_t previousEncoderState = 0;

bool isConnected = false;
bool requestInitialPreset = false;
bool waitingForInitialPreset = false;
unsigned long presetSyncStartedAt = 0;
byte currentEffect = 0;
const char *screenStatus = "SEARCHING...";
float batteryVoltage = 0.0f;
uint8_t batteryPercent = 0;

unsigned long lastBleStatusAt = 0;
unsigned long lastBatteryReadAt = 0;
unsigned long lastQuickPresetAChangeAt = 0;
unsigned long lastQuickPresetBChangeAt = 0;
bool lastQuickPresetAReading = HIGH;
bool lastQuickPresetBReading = HIGH;
bool quickPresetAState = HIGH;
bool quickPresetBState = HIGH;
unsigned long lastEncoderButtonChangeAt = 0;
bool lastEncoderButtonReading = HIGH;
bool encoderButtonState = HIGH;

uint8_t batteryPercentFromVoltage(float voltage)
{
  if (voltage <= BATTERY_EMPTY_VOLTAGE)
    return 0;
  if (voltage >= BATTERY_FULL_VOLTAGE)
    return 100;

  const float range = BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE;
  return (uint8_t)(((voltage - BATTERY_EMPTY_VOLTAGE) / range) * 100.0f + 0.5f);
}

void readBattery()
{
  // The TTGO board powers the battery divider only while measuring it.
  digitalWrite(PIN_BATTERY_ENABLE, HIGH);
  delayMicroseconds(100);

  uint32_t millivolts = 0;
  const uint8_t sampleCount = 8;
  for (uint8_t i = 0; i < sampleCount; i++)
    millivolts += analogReadMilliVolts(PIN_BATTERY_ADC);

  digitalWrite(PIN_BATTERY_ENABLE, LOW);

  batteryVoltage =
    (millivolts / (float)sampleCount) / 1000.0f * BATTERY_DIVIDER_RATIO;
  batteryPercent = batteryPercentFromVoltage(batteryVoltage);

  Serial.print("Battery: ");
  Serial.print(batteryVoltage, 2);
  Serial.print(" V (");
  Serial.print(batteryPercent);
  Serial.println("%)");
}

void drawBattery()
{
  const uint16_t batteryColor =
    batteryPercent <= 20 ? TFT_RED :
    batteryPercent <= 50 ? TFT_YELLOW :
    TFT_GREEN;

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("BATTERY", 4, 68, 1);

  tft.drawRect(4, 82, 62, 10, TFT_DARKGREY);
  tft.fillRect(66, 85, 3, 4, TFT_DARKGREY);
  const int fillWidth = (int)(56.0f * batteryPercent / 100.0f);
  if (fillWidth > 0)
    tft.fillRect(7, 85, fillWidth, 4, batteryColor);

  tft.setTextColor(batteryColor, TFT_BLACK);
  tft.drawString(String(batteryVoltage, 2) + "V", 4, 100, 1);
  tft.drawString(String(batteryPercent) + "%", 52, 100, 1);
}

void drawScreen()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  // The preset number is the primary information on the right.
  tft.drawFastVLine(99, 0, 135, TFT_DARKGREY);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("NUX FOOTSWITCH", 4, 4, 1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(screenStatus, 4, 20, 1);
  tft.drawString("GPIO35 -> P1", 4, 35, 1);
  tft.drawString("GPIO0  -> P5", 4, 46, 1);
  tft.drawString("ENC SW -> SEND", 4, 57, 1);
  drawBattery();

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("PRESET", 169, 17, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawNumber(currentEffect + 1, 169, 77, 8);
}

void showStatus(const char *status)
{
  screenStatus = status;
  drawScreen();
}

void showEffect()
{
  screenStatus = isConnected ? "CONNECTED" : "DISCONNECTED";
  drawScreen();
}

void updateBattery()
{
  if (millis() - lastBatteryReadAt < BATTERY_REFRESH_INTERVAL_MS)
    return;

  lastBatteryReadAt = millis();
  readBattery();
  drawScreen();
}

void requestCurrentPreset()
{
  // Mighty Plug Pro private SysEx request:
  // F0 43 58 70 0C 02 F7
  const byte request[] = {
    0xF0, 0x43, 0x58, 0x70, 0x0C, 0x02, 0xF7
  };

  Serial.println("Requesting current preset from NUX");
  MIDI.sendSysEx(sizeof(request), request, true);
}

void handleSystemExclusive(byte *data, unsigned size)
{
  Serial.print("SysEx RX (");
  Serial.print(size);
  Serial.print(" bytes): ");
  for (unsigned i = 0; i < size; i++) {
    if (data[i] < 0x10)
      Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  // NUX response after the request above:
  // F0 43 58 70 0C 03 <preset-index> 32 F7
  // Some BLE-MIDI transport versions leave timestamp bytes before F0.
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
      data[start + 6] < MAX_EFFECT_COUNT) {
    currentEffect = data[start + 6];
    waitingForInitialPreset = false;
    Serial.print("Current preset received from NUX: ");
    Serial.println(currentEffect + 1);
    showEffect();
  }
}

void sendCurrentEffect()
{
  // Mighty Plug Pro selects presets with MIDI Program Change, not CC 49.
  if (!isConnected) {
    Serial.println("Cannot send preset: BLE-MIDI is not connected");
    showEffect();
    return;
  }

  Serial.print("Sending preset: ");
  Serial.println(currentEffect + 1);
  MIDI.sendProgramChange(currentEffect, 1);
  showEffect();
}

void sendPresetNumber(uint8_t presetNumber)
{
  if (presetNumber < 1 || presetNumber > MAX_EFFECT_COUNT)
    return;

  currentEffect = presetNumber - 1;
  Serial.print("Quick preset selected: ");
  Serial.println(presetNumber);
  sendCurrentEffect();
}

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

void selectNextEffect()
{
  currentEffect = (currentEffect + 1) % MAX_EFFECT_COUNT;
  Serial.print("Selected preset: ");
  Serial.println(currentEffect + 1);
  showEffect();
}

void selectPreviousEffect()
{
  currentEffect =
    currentEffect == 0 ? MAX_EFFECT_COUNT - 1 : currentEffect - 1;
  Serial.print("Selected preset: ");
  Serial.println(currentEffect + 1);
  showEffect();
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
    selectNextEffect();
  else if (movement <= -4)
    selectPreviousEffect();
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

void readQuickPresetButtons()
{
  if (buttonWasPressed(
        PIN_QUICK_PRESET_A,
        lastQuickPresetAReading,
        quickPresetAState,
        lastQuickPresetAChangeAt
      )) {
    sendPresetNumber(QUICK_PRESET_A);
  }

  if (buttonWasPressed(
        PIN_QUICK_PRESET_B,
        lastQuickPresetBReading,
        quickPresetBState,
        lastQuickPresetBChangeAt
      )) {
    sendPresetNumber(QUICK_PRESET_B);
  }
}

void readEncoderButton()
{
  if (buttonWasPressed(
        PIN_ENCODER_SW,
        lastEncoderButtonReading,
        encoderButtonState,
        lastEncoderButtonChangeAt
      )) {
    Serial.println("Encoder pressed: sending selected preset");
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
  Serial.println("Quick presets: GPIO35=P1, GPIO0=P5");
  Serial.println("Encoder: CLK=25, DT=26, SW=27");

  // GPIO35 has no internal pull-up. The TTGO button circuit normally
  // provides the required bias; use an external pull-up for added switches.
  pinMode(PIN_QUICK_PRESET_A, INPUT);
  pinMode(PIN_QUICK_PRESET_B, INPUT_PULLUP);
  pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
  pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
  pinMode(PIN_ENCODER_SW, INPUT_PULLUP);

  previousEncoderState =
    (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);

  attachInterrupt(
    digitalPinToInterrupt(PIN_ENCODER_CLK),
    handleEncoder,
    CHANGE
  );
  attachInterrupt(
    digitalPinToInterrupt(PIN_ENCODER_DT),
    handleEncoder,
    CHANGE
  );

  pinMode(PIN_BATTERY_ENABLE, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);

  tft.init();
  tft.setRotation(1);
  tft.setTextFont(2);
  readBattery();
  lastBatteryReadAt = millis();
  showStatus("SEARCHING...");

  MIDI.begin(MIDI_CHANNEL_OMNI);

  BLEMIDI.setHandleConnected([]() {
    isConnected = true;
    requestInitialPreset = true;
    waitingForInitialPreset = true;
    Serial.println("BLE-MIDI connected to MIGHTY PLUG PRO");
    showStatus("CONNECTED");
  });

  BLEMIDI.setHandleDisconnected([]() {
    isConnected = false;
    requestInitialPreset = false;
    waitingForInitialPreset = false;
    Serial.println("BLE-MIDI disconnected; scanning");
    showStatus("SEARCHING...");
  });

  // Synchronize the display when the NUX sends a MIDI Program Change.
  MIDI.setHandleProgramChange([](byte channel, byte program) {
    if (program < MAX_EFFECT_COUNT) {
      currentEffect = program;
      waitingForInitialPreset = false;
      Serial.print("Preset received from NUX: ");
      Serial.println(currentEffect + 1);
      showEffect();
    }
  });
  MIDI.setHandleSystemExclusive(handleSystemExclusive);

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
  readQuickPresetButtons();
  readEncoder();
  readEncoderButton();
  updateBattery();

  if (!isConnected) {
    if (millis() - lastBleStatusAt >= 2000) {
      lastBleStatusAt = millis();
      Serial.println("BLE: searching for MIGHTY PLUG PRO...");
    }
    delay(5);
    return;
  }

  if (requestInitialPreset) {
    // Let BLE-MIDI finish settling before asking the device for its state.
    delay(250);
    if (!isConnected) {
      requestInitialPreset = false;
      waitingForInitialPreset = false;
      return;
    }

    showStatus("SYNC...");
    presetSyncStartedAt = millis();
    requestCurrentPreset();
    requestInitialPreset = false;
  }

  if (waitingForInitialPreset &&
      millis() - presetSyncStartedAt >= PRESET_SYNC_TIMEOUT_MS) {
    waitingForInitialPreset = false;
    Serial.println(
      "No current preset response; using local preset selection"
    );
    showEffect();
  }

  delay(1);
}