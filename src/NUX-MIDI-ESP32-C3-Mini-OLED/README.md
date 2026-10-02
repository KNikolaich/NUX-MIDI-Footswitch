# NUX MIDI preset switch — ESP32-C3 Mini OLED

Standalone minimal BLE-MIDI footswitch for NUX Mighty Plug Pro / MP-3.

## Controls

- BOOT single click: select preset 1 (`Program Change`, channel 1, value `0`).
- BOOT double click: select preset 3 (`Program Change`, channel 1, value `2`).

The button waits up to 350 ms after release to distinguish a single click from a
double click. Preset commands are sent only while BLE-MIDI is connected.

## Assumed hardware

This sketch assumes the common ESP32-C3 Mini board with:

- onboard BOOT button on GPIO9, active LOW;
- onboard 0.42-inch SSD1306 OLED, 72×40 pixels;
- I2C SDA on GPIO5, SCL on GPIO6;
- I2C address `0x3C`.

Board variants may use a different OLED controller, address, or BOOT pin. Check
the exact board documentation if the screen or button does not respond.

Holding BOOT while powering on or resetting the ESP32-C3 enters its ROM
download mode. Do not hold the button during normal startup.

## Arduino IDE

1. Open `NUX-MIDI-ESP32-C3-Mini-OLED.ino`.
2. Select the matching ESP32-C3 board.
3. Install Arduino BLE-MIDI, MIDI Library, NimBLE-Arduino, and U8g2.
4. Upload, then open Serial Monitor at 115200 baud.
5. Connect the NUX to power and wait for the display to show `BLE READY`.

This sketch intentionally uses only Program Change; it does not send CC or SysEx.