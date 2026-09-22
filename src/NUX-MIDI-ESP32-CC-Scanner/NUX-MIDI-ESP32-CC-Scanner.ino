/**
 * ESP32 DevKit BLE-MIDI Control Change scanner for NUX Mighty Plug Pro.
 *
 * This is an experimental project, separate from the normal footswitch
 * sketches in this repository.
 *
 * Features:
 * - ESP32 SoftAP with saved SSID/password and HTTP Basic Auth.
 * - Browser pages for safe CC sweeps, one-shot CC commands and MIDI capture.
 * - OTA firmware upload from the browser.
 * - Saved NUX BLE target, WiFi and web credentials in ESP32 NVS.
 * - Serial menu for starting/stopping MIDI capture without WiFi.
 *
 * Safety boundaries:
 * - No SysEx is sent or registered for capture.
 * - RPN/NRPN, bank select and system/reset CC numbers are blocked.
 * - CC sweep interval cannot be shorter than 250 ms.
 *
 * Required libraries:
 * - Arduino BLE-MIDI by lathoub
 * - MIDI Library by FortySevenEffects
 * - NimBLE-Arduino compatible with the installed BLE-MIDI version
 *
 * Target board: ESP32 Dev Module / ESP32-WROOM-32.
 * Serial Monitor: 115200 baud.
 */

#include <Arduino.h>

#include "AppConfig.h"
#include "MidiScanner.h"
#include "SerialMenu.h"
#include "SweepScanner.h"
#include "WebInterface.h"

void setup()
{
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("NUX MIDI CC scanner starting");
  Serial.println("Board: ESP32 DevKit / ESP32-WROOM-32");
  Serial.println("Safety: CC only; SysEx disabled");

  appConfigBegin();
  const AppConfig &config = appConfigGet();
  Serial.print("Configured BLE target: ");
  Serial.println(config.midiTarget);

  sweepScannerBegin();
  midiScannerBegin();
  webInterfaceBegin();
  serialMenuBegin();
}

void loop()
{
  webInterfaceLoop();
  sweepScannerLoop();
  serialMenuLoop();
  delay(1);
}