#include "SerialMenu.h"

#include "MidiScanner.h"

namespace
{
  void printMenu()
  {
    Serial.println();
    Serial.println("Serial menu:");
    Serial.println("  1 - start MIDI capture");
    Serial.println("  2 - stop MIDI capture");
    Serial.println("  3 - show status");
    Serial.println("  4 - show HTTP endpoints");
    Serial.println("  h - show this menu");
  }

  void printStatus()
  {
    Serial.print("BLE connected: ");
    Serial.println(midiScannerIsConnected() ? "yes" : "no");
    Serial.print("BLE target: ");
    Serial.println(midiScannerTarget());
    Serial.print("MIDI capture: ");
    Serial.println(midiScannerCaptureEnabled() ? "on" : "off");
  }

  void printEndpoints()
  {
    Serial.println("HTTP endpoints:");
    Serial.println("  http://192.168.4.1/");
    Serial.println("  /scan       CC sweep and single-command test");
    Serial.println("  /capture    incoming CC/Program Change capture");
    Serial.println("  /settings   saved WiFi/BLE parameters");
    Serial.println("  /ota        firmware upload");
    Serial.println("  /midi/cc    single Control Change POST endpoint");
}

}

void serialMenuBegin()
{
  printMenu();
}

void serialMenuLoop()
{
  if (!Serial.available())
    return;

  const char command = (char)Serial.read();
  switch (command) {
    case '1':
      midiScannerSetCapture(true);
      break;
    case '2':
      midiScannerSetCapture(false);
      break;
    case '3':
      printStatus();
      break;
    case '4':
      printEndpoints();
      break;
    case 'h':
    case 'H':
      printMenu();
      break;
    default:
      break;
  }
}