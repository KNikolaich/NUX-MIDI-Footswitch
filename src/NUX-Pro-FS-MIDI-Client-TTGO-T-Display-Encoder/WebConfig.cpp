#include "WebConfig.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

namespace
{
  constexpr char AP_SSID[] = "NUX-TTGO-Setup";
  constexpr char AP_PASSWORD[] = "nux12345";
  constexpr char WEB_USER[] = "admin";
  constexpr char WEB_PASSWORD[] = "nux12345";
  constexpr uint32_t RESTART_DELAY_MS = 1200;

  WebServer server(80);
  DeviceSettings *activeSettings = nullptr;
  uint32_t restartAt = 0;
  bool updateStarted = false;
  bool updateSucceeded = false;

  bool requireAuthentication()
  {
    if (server.authenticate(WEB_USER, WEB_PASSWORD))
      return true;
    server.requestAuthentication(BASIC_AUTH, "NUX TTGO settings");
    return false;
  }

  String escapeHtml(const String &value)
  {
    String result;
    result.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); i++) {
      switch (value[i]) {
        case '&': result += F("&amp;"); break;
        case '<': result += F("&lt;"); break;
        case '>': result += F("&gt;"); break;
        case '"': result += F("&quot;"); break;
        case '\'': result += F("&#39;"); break;
        default: result += value[i]; break;
      }
    }
    return result;
  }

  String presetOptions(uint8_t selected)
  {
    String options;
    for (uint8_t preset = 1; preset <= 7; preset++) {
      options += F("<option value=\"");
      options += preset;
      options += '"';
      if (preset == selected)
        options += F(" selected");
      options += F(">Preset ");
      options += preset;
      options += F("</option>");
    }
    return options;
  }

  String settingsPage()
  {
    String page = F(
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>NUX TTGO settings</title><style>"
      "body{font:16px system-ui,sans-serif;max-width:560px;margin:30px auto;"
      "padding:0 16px;background:#101820;color:#edf3f8}"
      "section{background:#1d2a35;padding:20px;border-radius:12px;margin:16px 0}"
      "label{display:block;margin:14px 0 6px}input,select,button{font:inherit;"
      "padding:10px;border-radius:7px;border:1px solid #526575;width:100%;"
      "box-sizing:border-box}button{margin-top:18px;background:#20b8a6;color:#071412;"
      "font-weight:700;border:0}a{color:#72dacf}</style></head><body>"
      "<h1>NUX TTGO</h1><p>BLE target and quick preset buttons</p><section>"
      "<form method=\"post\" action=\"/save\">"
      "<label>BLE device name or MAC address (max 22 characters)</label>"
      "<input name=\"target\" maxlength=\"22\" required value=\"@TARGET@\">"
      "<label>GPIO35 quick preset</label><select name=\"presetA\">@PRESET_A@</select>"
      "<label>GPIO0 quick preset</label><select name=\"presetB\">@PRESET_B@</select>"
      "<button type=\"submit\">Save and restart</button></form></section>"
      "<section><h2>Firmware</h2><a href=\"/update\">Open OTA firmware update</a>"
      "</section></body></html>"
    );
    page.replace("@TARGET@", escapeHtml(activeSettings->bleTarget));
    page.replace("@PRESET_A@", presetOptions(activeSettings->presetA));
    page.replace("@PRESET_B@", presetOptions(activeSettings->presetB));
    return page;
  }

  void handleRoot()
  {
    if (!requireAuthentication())
      return;
    server.sendHeader("Location", "/settings");
    server.send(302, "text/plain", "Open /settings");
  }

  void handleSettings()
  {
    if (!requireAuthentication())
      return;
    server.send(200, "text/html; charset=utf-8", settingsPage());
  }

  void handleSave()
  {
    if (!requireAuthentication())
      return;

    String target = server.arg("target");
    target.trim();
    const long presetA = server.arg("presetA").toInt();
    const long presetB = server.arg("presetB").toInt();

    if (presetA < 1 || presetA > 7 || presetB < 1 || presetB > 7 ||
        target.length() == 0 || target.length() > 22) {
      server.send(400, "text/plain",
                  "Invalid target or preset number; presets must be 1 to 7.");
      return;
    }

    if (!deviceSettingsSave(
          *activeSettings,
          target,
          (uint8_t)presetA,
          (uint8_t)presetB)) {
      server.send(500, "text/plain", "Could not save settings to NVS.");
      return;
    }

    server.send(200, "text/html; charset=utf-8",
      "<!doctype html><meta charset=\"utf-8\"><p>Saved. Device restarting.</p>");
    restartAt = millis() + RESTART_DELAY_MS;
  }

  void handleUpdatePage()
  {
    if (!requireAuthentication())
      return;
    server.send(200, "text/html; charset=utf-8",
      "<!doctype html><html><head><meta name=\"viewport\" "
      "content=\"width=device-width,initial-scale=1\"><title>OTA</title>"
      "<style>body{font:16px system-ui;max-width:520px;margin:30px auto;"
      "padding:0 16px}input,button{font:inherit;padding:10px;margin:10px 0}"
      "</style></head><body><h1>Firmware update</h1>"
      "<p>Select an ESP32 .bin file. Keep power connected during upload.</p>"
      "<form method=\"post\" action=\"/update\" enctype=\"multipart/form-data\">"
      "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
      "<button type=\"submit\">Upload firmware</button></form>"
      "<p><a href=\"/settings\">Back to settings</a></p></body></html>");
  }

  void handleUpdateUpload()
  {
    if (!requireAuthentication())
      return;

    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      updateStarted = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      updateSucceeded = false;
      if (!updateStarted) {
        Serial.println("OTA begin failed; verify the partition table has OTA slots.");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE && updateStarted) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        updateStarted = false;
        Serial.println("OTA write failed.");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END && updateStarted) {
      updateSucceeded = Update.end(true);
      updateStarted = false;
      if (updateSucceeded) {
        Serial.printf("OTA success: %u bytes\n", upload.totalSize);
      } else {
        Serial.println("OTA finalize failed.");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      updateStarted = false;
      updateSucceeded = false;
      Update.abort();
      Serial.println("OTA upload aborted.");
    }
  }

  void handleUpdateComplete()
  {
    if (!requireAuthentication())
      return;

    if (!updateSucceeded) {
      server.send(500, "text/plain",
        "OTA failed. Confirm an OTA-capable partition scheme and retry.");
      return;
    }

    server.send(200, "text/html; charset=utf-8",
      "<!doctype html><meta charset=\"utf-8\"><p>Update complete. Restarting.</p>");
    restartAt = millis() + RESTART_DELAY_MS;
  }
}

void webConfigBegin(DeviceSettings &settings)
{
  activeSettings = &settings;
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Could not start settings WiFi access point.");
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateComplete, handleUpdateUpload);
  server.onNotFound([]() {
    if (!requireAuthentication())
      return;
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  Serial.print("Settings AP: ");
  Serial.println(AP_SSID);
  Serial.print("Settings page: http://");
  Serial.println(WiFi.softAPIP());
}

void webConfigHandleClient()
{
  server.handleClient();
}

bool webConfigRestartRequested()
{
  return restartAt != 0 &&
         (int32_t)(millis() - restartAt) >= 0;
}