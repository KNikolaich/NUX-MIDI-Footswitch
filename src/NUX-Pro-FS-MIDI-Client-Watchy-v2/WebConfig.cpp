#include "WebConfig.h"

#include <Update.h>
#include <WiFi.h>

#include "FirmwareVersion.h"
#include "WatchyConfig.h"

namespace
{
  constexpr uint32_t RESTART_DELAY_MS = 1200;

  bool validTargetInput(const String &target)
  {
    if (target.length() == 0 || target.length() > 22)
      return false;
    for (size_t i = 0; i < target.length(); i++) {
      const unsigned char ch = target[i];
      if (ch < 0x20 || ch > 0x7e)
        return false;
    }
    return true;
  }

  String escapeHtml(const String &value)
  {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); i++) {
      switch (value[i]) {
        case '&': escaped += F("&amp;"); break;
        case '<': escaped += F("&lt;"); break;
        case '>': escaped += F("&gt;"); break;
        case '"': escaped += F("&quot;"); break;
        case '\'': escaped += F("&#39;"); break;
        default: escaped += value[i]; break;
      }
    }
    return escaped;
  }

  String presetOptions(uint8_t selected)
  {
    String options;
    for (uint8_t preset = 1; preset <= WatchyConfig::PRESET_COUNT; preset++) {
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

  String doubleTapActionOptions(DoubleTapAction selected)
  {
    const DoubleTapAction ACTIONS[] = {
      DoubleTapAction::Disabled,
      DoubleTapAction::CycleNext,
      DoubleTapAction::FixedPreset
    };
    const char *VALUES[] = {"off", "cycle", "fixed"};
    const char *LABELS[] = {
      "Disabled",
      "Cycle to next preset",
      "Use fixed preset"
    };

    String options;
    for (uint8_t i = 0; i < 3; ++i) {
      options += F("<option value=\"");
      options += VALUES[i];
      options += '"';
      if (ACTIONS[i] == selected)
        options += F(" selected");
      options += '>';
      options += LABELS[i];
      options += F("</option>");
    }
    return options;
  }

  bool parseDoubleTapAction(
    const String &value,
    DoubleTapAction &action)
  {
    if (value == "off") {
      action = DoubleTapAction::Disabled;
      return true;
    }
    if (value == "cycle") {
      action = DoubleTapAction::CycleNext;
      return true;
    }
    if (value == "fixed") {
      action = DoubleTapAction::FixedPreset;
      return true;
    }
    return false;
  }
}

WebConfig::WebConfig()
  : _server(80)
{
}

void WebConfig::begin(DeviceSettingsStore &settingsStore)
{
  _settingsStore = &settingsStore;
  registerRoutes();
  // The access point is intentionally off during normal operation.
  WiFi.mode(WIFI_OFF);
}

void WebConfig::registerRoutes()
{
  if (_routesRegistered)
    return;

  _server.on("/", HTTP_GET, [this]() { handleRoot(); });
  _server.on("/settings", HTTP_GET, [this]() { handleSettings(); });
  _server.on("/save", HTTP_POST, [this]() { handleSave(); });
  _server.on("/update", HTTP_GET, [this]() { handleUpdatePage(); });
  _server.on(
    "/update",
    HTTP_POST,
    [this]() { handleUpdateComplete(); },
    [this]() { handleUpdateUpload(); }
  );
  _server.onNotFound([this]() {
    if (!requireAuthentication())
      return;
    _server.send(404, "text/plain", "Not found");
  });
  _routesRegistered = true;
}

bool WebConfig::startPortal()
{
  if (_portalActive)
    return true;

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  if (!WiFi.softAP(
        WatchyConfig::AP_SSID,
        WatchyConfig::AP_PASSWORD)) {
    Serial.println("[WEB] Could not start settings access point");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  _server.begin();
  _portalActive = true;
  _lastActivityAt = millis();

  Serial.println("[WEB] Settings and OTA portal is active");
  Serial.printf("      WiFi: %s / %s\n",
                WatchyConfig::AP_SSID,
                WatchyConfig::AP_PASSWORD);
  Serial.printf("      URL: http://%s/\n",
                WiFi.softAPIP().toString().c_str());
  Serial.printf("      Login: %s / %s\n",
                WatchyConfig::WEB_USERNAME,
                WatchyConfig::WEB_PASSWORD);
  Serial.println("      Endpoints: /settings, /save, /update");
  Serial.println("      Portal stops after 3 minutes without requests.");
  return true;
}

void WebConfig::stopPortal()
{
  if (!_portalActive)
    return;

  if (_updateStarted) {
    Update.abort();
    _updateStarted = false;
  }
  _updateSucceeded = false;
  _server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  _portalActive = false;
  Serial.println("[WEB] Access point stopped; WiFi radio is off");
}

void WebConfig::togglePortal()
{
  if (_portalActive)
    stopPortal();
  else if (!startPortal())
    Serial.println("[WEB] Portal could not be enabled");
}

void WebConfig::loop()
{
  if (!_portalActive)
    return;

  _server.handleClient();
  if (!_restartAt &&
      millis() - _lastActivityAt >=
        WatchyConfig::PORTAL_IDLE_TIMEOUT_MS) {
    Serial.println("[WEB] Portal idle timeout reached");
    stopPortal();
  }
}

bool WebConfig::portalActive() const
{
  return _portalActive;
}

bool WebConfig::restartRequested() const
{
  return _restartAt != 0 &&
         (int32_t)(millis() - _restartAt) >= 0;
}

void WebConfig::touchActivity()
{
  _lastActivityAt = millis();
}

bool WebConfig::requireAuthentication()
{
  if (_server.authenticate(
        WatchyConfig::WEB_USERNAME,
        WatchyConfig::WEB_PASSWORD)) {
    touchActivity();
    return true;
  }
  _server.requestAuthentication(BASIC_AUTH, "NUX Watchy v2 settings");
  return false;
}

String WebConfig::settingsPage() const
{
  const DeviceSettings &settings = _settingsStore->get();
  String page = F(
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>NUX Watchy settings</title><style>"
    "body{font:16px system-ui,sans-serif;max-width:600px;margin:28px auto;"
    "padding:0 16px;background:#101820;color:#edf3f8}"
    "section{background:#1d2a35;padding:18px;border-radius:12px;margin:16px 0}"
    "label{display:block;margin:12px 0 5px}input,select,button{font:inherit;"
    "padding:10px;border-radius:7px;border:1px solid #526575;width:100%;"
    "box-sizing:border-box}button{margin-top:18px;background:#20b8a6;color:#071412;"
    "font-weight:700;border:0}a{color:#72dacf}.fixed-preset{margin:4px 0 14px}"
    "</style></head><body>"
    "<h1>NUX MIDI Footswitch</h1><p>Firmware v@FW_VERSION@ · Hardware Watchy v2.0</p>"
    "<p>Cycle changes the short-press preset; a fixed preset does not.</p>"
    "<section><form method=\"post\" action=\"/save\">"
    "<label>BLE target name or MAC (up to 22 ASCII characters)</label>"
    "<input name=\"target\" maxlength=\"22\" required value=\"@TARGET@\">"
    "<label>MENU / GPIO26 — short press preset</label>"
    "<select name=\"menu\">@MENU@</select>"
    "<label>MENU double-click action</label>"
    "<select id=\"menuAction\" name=\"menuAction\">@MENU_ACTION@</select>"
    "<div id=\"menuFixedWrap\" class=\"fixed-preset\">"
    "<label>Preset for double-click</label>"
    "<select name=\"menuDoublePreset\">@MENU_DOUBLE_PRESET@</select></div>"
    "<label>BACK / GPIO25 — short press preset</label>"
    "<select name=\"back\">@BACK@</select>"
    "<label>BACK double-click action</label>"
    "<select id=\"backAction\" name=\"backAction\">@BACK_ACTION@</select>"
    "<div id=\"backFixedWrap\" class=\"fixed-preset\">"
    "<label>Preset for double-click</label>"
    "<select name=\"backDoublePreset\">@BACK_DOUBLE_PRESET@</select></div>"
    "<label>UP / GPIO35 — short press preset</label>"
    "<select name=\"up\">@UP@</select>"
    "<label>UP double-click action</label>"
    "<select id=\"upAction\" name=\"upAction\">@UP_ACTION@</select>"
    "<div id=\"upFixedWrap\" class=\"fixed-preset\">"
    "<label>Preset for double-click</label>"
    "<select name=\"upDoublePreset\">@UP_DOUBLE_PRESET@</select></div>"
    "<label>DOWN / GPIO4 — short press preset</label>"
    "<select name=\"down\">@DOWN@</select>"
    "<label>DOWN double-click action</label>"
    "<select id=\"downAction\" name=\"downAction\">@DOWN_ACTION@</select>"
    "<div id=\"downFixedWrap\" class=\"fixed-preset\">"
    "<label>Preset for double-click</label>"
    "<select name=\"downDoublePreset\">@DOWN_DOUBLE_PRESET@</select></div>"
    "<button type=\"submit\">Save and restart</button></form></section>"
    "<script>"
    "function syncDoublePreset(key){"
    "const action=document.getElementById(key+'Action').value;"
    "document.getElementById(key+'FixedWrap').hidden=action!=='fixed';}"
    "window.addEventListener('DOMContentLoaded',function(){"
    "['menu','back','up','down'].forEach(function(key){"
    "const select=document.getElementById(key+'Action');"
    "select.addEventListener('change',function(){syncDoublePreset(key);});"
    "syncDoublePreset(key);});});"
    "</script>"
    "<section><h2>Firmware</h2><a href=\"/update\">Open OTA firmware update</a>"
    "<p>Keep the watch connected to USB power during the update.</p>"
    "</section></body></html>"
  );

  page.replace("@MENU@", presetOptions(settings.presets[0]));
  page.replace("@BACK@", presetOptions(settings.presets[1]));
  page.replace("@UP@", presetOptions(settings.presets[2]));
  page.replace("@DOWN@", presetOptions(settings.presets[3]));
  page.replace("@MENU_ACTION@", doubleTapActionOptions(settings.doubleTapActions[0]));
  page.replace("@BACK_ACTION@", doubleTapActionOptions(settings.doubleTapActions[1]));
  page.replace("@UP_ACTION@", doubleTapActionOptions(settings.doubleTapActions[2]));
  page.replace("@DOWN_ACTION@", doubleTapActionOptions(settings.doubleTapActions[3]));
  page.replace("@MENU_DOUBLE_PRESET@", presetOptions(settings.doubleTapPresets[0]));
  page.replace("@BACK_DOUBLE_PRESET@", presetOptions(settings.doubleTapPresets[1]));
  page.replace("@UP_DOUBLE_PRESET@", presetOptions(settings.doubleTapPresets[2]));
  page.replace("@DOWN_DOUBLE_PRESET@", presetOptions(settings.doubleTapPresets[3]));
  page.replace("@FW_VERSION@", FirmwareVersion::STRING);
  page.replace("@TARGET@", escapeHtml(settings.bleTarget));
  return page;
}

void WebConfig::handleRoot()
{
  if (!requireAuthentication())
    return;
  _server.sendHeader("Location", "/settings");
  _server.send(302, "text/plain", "Open /settings");
}

void WebConfig::handleSettings()
{
  if (!requireAuthentication())
    return;
  _server.send(200, "text/html; charset=utf-8", settingsPage());
}

void WebConfig::handleSave()
{
  if (!requireAuthentication())
    return;

  String target = _server.arg("target");
  target.trim();

  const long menu = _server.arg("menu").toInt();
  const long back = _server.arg("back").toInt();
  const long up = _server.arg("up").toInt();
  const long down = _server.arg("down").toInt();

  const char *actionArguments[WatchyConfig::BUTTON_COUNT] = {
    "menuAction", "backAction", "upAction", "downAction"
  };
  const char *doublePresetArguments[WatchyConfig::BUTTON_COUNT] = {
    "menuDoublePreset",
    "backDoublePreset",
    "upDoublePreset",
    "downDoublePreset"
  };
  DoubleTapAction doubleTapActions[WatchyConfig::BUTTON_COUNT];
  uint8_t doubleTapPresets[WatchyConfig::BUTTON_COUNT];
  bool validDoubleTapSettings = true;
  for (uint8_t i = 0; i < WatchyConfig::BUTTON_COUNT; ++i) {
    if (!parseDoubleTapAction(
          _server.arg(actionArguments[i]),
          doubleTapActions[i])) {
      validDoubleTapSettings = false;
      break;
    }

    const long doubleTapPreset =
      _server.arg(doublePresetArguments[i]).toInt();
    if (doubleTapPreset < 1 ||
        doubleTapPreset > WatchyConfig::PRESET_COUNT) {
      validDoubleTapSettings = false;
      break;
    }
    doubleTapPresets[i] = static_cast<uint8_t>(doubleTapPreset);
  }

  if (!validTargetInput(target) ||
      menu < 1 || menu > WatchyConfig::PRESET_COUNT ||
      back < 1 || back > WatchyConfig::PRESET_COUNT ||
      up < 1 || up > WatchyConfig::PRESET_COUNT ||
      down < 1 || down > WatchyConfig::PRESET_COUNT ||
      !validDoubleTapSettings) {
    _server.send(400, "text/plain",
      "Invalid target, preset assignment, or double-click action.");
    return;
  }

  const uint8_t presets[WatchyConfig::BUTTON_COUNT] = {
    (uint8_t)menu, (uint8_t)back, (uint8_t)up, (uint8_t)down
  };
  if (!_settingsStore->save(
        target, presets, doubleTapActions, doubleTapPresets)) {
    _server.send(500, "text/plain", "Could not save settings to NVS.");
    return;
  }

  _server.send(200, "text/html; charset=utf-8",
    "<!doctype html><meta charset=\"utf-8\">"
    "<p>Settings saved. Watchy is restarting.</p>");
  _restartAt = millis() + RESTART_DELAY_MS;
}

void WebConfig::handleUpdatePage()
{
  if (!requireAuthentication())
    return;
  _server.send(200, "text/html; charset=utf-8",
    "<!doctype html><html><head><meta name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>Watchy OTA</title>"
    "<style>body{font:16px system-ui;max-width:520px;margin:28px auto;"
    "padding:0 16px}input,button{font:inherit;padding:10px;margin:10px 0}"
    "</style></head><body><h1>Firmware update</h1>"
    "<p>Select the compiled Watchy .bin file. Do not disconnect power.</p>"
    "<form method=\"post\" action=\"/update\" enctype=\"multipart/form-data\">"
    "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
    "<button type=\"submit\">Upload firmware</button></form>"
    "<p><a href=\"/settings\">Back to settings</a></p></body></html>");
}

void WebConfig::handleUpdateUpload()
{
  if (!requireAuthentication())
    return;

  HTTPUpload &upload = _server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    _updateSucceeded = false;
    _updateStarted = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    if (!_updateStarted) {
      Serial.println("[OTA] Begin failed; check that partition scheme has OTA slots");
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE && _updateStarted) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      _updateStarted = false;
      Update.abort();
      Serial.println("[OTA] Firmware write failed");
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END && _updateStarted) {
    _updateSucceeded = Update.end(true);
    _updateStarted = false;
    if (_updateSucceeded)
      Serial.printf("[OTA] Image received: %u bytes\n", upload.totalSize);
    else {
      Serial.println("[OTA] Finalization failed");
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    _updateStarted = false;
    _updateSucceeded = false;
    Update.abort();
    Serial.println("[OTA] Upload aborted");
  }
}

void WebConfig::handleUpdateComplete()
{
  if (!requireAuthentication())
    return;
  if (!_updateSucceeded) {
    _server.send(500, "text/plain",
      "OTA failed. Use an OTA-capable partition scheme and retry.");
    return;
  }

  _server.send(200, "text/html; charset=utf-8",
    "<!doctype html><meta charset=\"utf-8\">"
    "<p>Update complete. Watchy is restarting.</p>");
  _restartAt = millis() + RESTART_DELAY_MS;
}