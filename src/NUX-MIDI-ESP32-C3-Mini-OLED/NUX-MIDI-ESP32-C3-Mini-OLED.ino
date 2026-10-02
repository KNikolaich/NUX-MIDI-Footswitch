/**
 * Minimal BLE-MIDI preset footswitch for NUX MIGHTY PLUG PRO / MP-3.
 *
 * Board: ESP32-C3 Mini with onboard 0.42" SSD1306 OLED (72x40).
 * OLED: SDA=GPIO5, SCL=GPIO6, I2C address=0x3C, visible area offset X=28/Y=0.
 * U8g2's SSD1306_72X40_ER driver already applies the X=28 controller offset.
 * BOOT button: GPIO9, active LOW. RST is a separate hardware reset button.
 * Board indicator LEDs: PWR=GPIO4 and USB=GPIO8; this sketch does not use them.
 *
 * Single click selects preset 1 (MIDI Program Change value 0).
 * Double click selects preset 3 (MIDI Program Change value 2).
 *
 * Required Arduino libraries:
 * - Arduino BLE-MIDI by lathoub
 * - MIDI Library by FortySevenEffects
 * - NimBLE-Arduino (legacy BLE-MIDI transport dependency)
 * - U8g2
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>

#define MIDI_DEVICE_NAME "MIGHTY PLUG PRO"
#define PIN_BOOT_BUTTON 9
#define OLED_SDA 5
#define OLED_SCL 6
#define OLED_ADDRESS 0x3C
#define CLICK_WINDOW_MS 350
#define BUTTON_DEBOUNCE_MS 30

U8G2_SSD1306_72X40_ER_F_HW_I2C display(
  U8G2_R0,
  U8X8_PIN_NONE,
  OLED_SCL,
  OLED_SDA
);

BLEMIDI_CREATE_INSTANCE(MIDI_DEVICE_NAME, MIDI)

volatile bool bleConnected = false;
bool displayedConnectionState = false;
bool buttonRawState = HIGH;
bool buttonStableState = HIGH;
unsigned long buttonRawChangedAt = 0;
unsigned long lastClickAt = 0;
uint8_t pendingClickCount = 0;
uint8_t selectedPreset = 0;
bool presetDisplayPending = false;

void drawScreen(const char *status)
{
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 10, "NUX MP-3");
  display.drawHLine(0, 13, 72);

  display.setFont(u8g2_font_10x20_tf);
  char presetLabel[8];
  if (selectedPreset == 0) {
    snprintf(presetLabel, sizeof(presetLabel), "P-");
  } else {
    snprintf(presetLabel, sizeof(presetLabel), "P%u", selectedPreset);
  }
  const int16_t textWidth = display.getStrWidth(presetLabel);
  display.drawStr((72 - textWidth) / 2, 33, presetLabel);

  display.setFont(u8g2_font_4x6_tf);
  const int16_t statusWidth = display.getStrWidth(status);
  display.drawStr((72 - statusWidth) / 2, 39, status);
  display.sendBuffer();
}

void selectPreset(uint8_t programNumber)
{
  if (!bleConnected) {
    Serial.println("Preset not sent: BLE-MIDI is disconnected");
    drawScreen("BLE OFFLINE");
    return;
  }

  selectedPreset = programNumber + 1;
  MIDI.sendProgramChange(programNumber, 1);
  Serial.print("Sent Program Change: preset ");
  Serial.println(selectedPreset);
  presetDisplayPending = true;
}

void handleButtonClick()
{
  if (pendingClickCount == 0) {
    pendingClickCount = 1;
    lastClickAt = millis();
  } else if (pendingClickCount == 1 &&
             millis() - lastClickAt <= CLICK_WINDOW_MS) {
    pendingClickCount = 2;
    lastClickAt = millis();
  }
}

void readBootButton()
{
  const bool reading = digitalRead(PIN_BOOT_BUTTON);
  const unsigned long now = millis();

  if (reading != buttonRawState) {
    buttonRawState = reading;
    buttonRawChangedAt = now;
  }

  if (now - buttonRawChangedAt >= BUTTON_DEBOUNCE_MS &&
      buttonStableState != buttonRawState) {
    buttonStableState = buttonRawState;
    if (buttonStableState == HIGH) {
      handleButtonClick();
    }
  }

  if (pendingClickCount != 0 &&
      now - lastClickAt > CLICK_WINDOW_MS) {
    const uint8_t clicks = pendingClickCount;
    pendingClickCount = 0;
    selectPreset(clicks == 1 ? 0 : 2);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(150);

  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  Wire.begin(OLED_SDA, OLED_SCL);
  display.setI2CAddress(OLED_ADDRESS << 1);
  display.setBusClock(400000);
  display.begin();
  drawScreen("BLE SEARCH");

  Serial.println();
  Serial.println("NUX MP-3 C3 Mini preset switch");
  Serial.println("BOOT single click: preset 1");
  Serial.println("BOOT double click: preset 3");

  MIDI.begin(MIDI_CHANNEL_OMNI);
  BLEMIDI.setHandleConnected([]() {
    bleConnected = true;
    Serial.println("BLE-MIDI connected to MIGHTY PLUG PRO");
  });
  BLEMIDI.setHandleDisconnected([]() {
    bleConnected = false;
    Serial.println("BLE-MIDI disconnected; scanning");
  });
}

void loop()
{
  MIDI.read();
  readBootButton();

  const bool connected = bleConnected;
  if (connected != displayedConnectionState) {
    displayedConnectionState = connected;
    if (connected) {
      drawScreen("BLE READY");
    } else {
      drawScreen("BLE SEARCH");
    }
  }

  if (presetDisplayPending) {
    presetDisplayPending = false;
    drawScreen(bleConnected ? "BLE READY" : "BLE OFFLINE");
  }

  delay(1);
}