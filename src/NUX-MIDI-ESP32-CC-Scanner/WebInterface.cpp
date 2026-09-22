#include "WebInterface.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "MidiScanner.h"
#include "SweepScanner.h"

namespace
{
  WebServer server(80);
  unsigned long restartAt = 0;

  const char *stateName(SweepState state)
  {
    switch (state) {
      case SWEEP_RUNNING:
        return "running";
      case SWEEP_PAUSED:
        return "paused";
      default:
        return "stopped";
    }
  }

  bool requireAuth()
  {
    const AppConfig &config = appConfigGet();
    if (server.authenticate(config.webUser.c_str(), config.webPassword.c_str()))
      return true;

    server.requestAuthentication();
    return false;
  }

  String jsonEscape(const String &value)
  {
    String result;
    result.reserve(value.length() + 16);
    for (size_t i = 0; i < value.length(); i++) {
      const char character = value[i];
      switch (character) {
        case '\\':
          result += "\\\\";
          break;
        case '"':
          result += "\\\"";
          break;
        case '\n':
          result += "\\n";
          break;
        case '\r':
          result += "\\r";
          break;
        case '\t':
          result += "\\t";
          break;
        default:
          result += character;
          break;
      }
    }
    return result;
  }

  String htmlEscape(const String &value)
  {
    String result;
    result.reserve(value.length() + 16);
    for (size_t i = 0; i < value.length(); i++) {
      switch (value[i]) {
        case '&':
          result += "&amp;";
          break;
        case '<':
          result += "&lt;";
          break;
        case '>':
          result += "&gt;";
          break;
        case '"':
          result += "&quot;";
          break;
        default:
          result += value[i];
          break;
      }
    }
    return result;
  }

  void sendJson(const String &body, int code = 200)
  {
    server.send(code, "application/json; charset=utf-8", body);
  }

  bool argumentNumber(
    const char *name,
    int minimum,
    int maximum,
    int &result
  )
  {
    if (!server.hasArg(name))
      return false;

    const String value = server.arg(name);
    if (value.length() == 0)
      return false;

    result = value.toInt();
    return result >= minimum && result <= maximum &&
           String(result) == value;
  }

  String pageStart(const String &title)
  {
    String html = R"HTML(<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>)HTML";
    html += title;
    html += R"HTML(</title>
<style>
:root{font-family:Arial,sans-serif;color:#18202a;background:#eef1f5}
body{margin:0}.wrap{max-width:1050px;margin:auto;padding:16px}
nav{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:16px}
nav a,button{border:0;border-radius:6px;background:#1769aa;color:white;
padding:10px 14px;text-decoration:none;cursor:pointer;font-size:15px}
nav a{display:inline-block}button.secondary{background:#64748b}
button.danger{background:#b42318}button.warn{background:#a15c00}
section{background:white;border-radius:10px;padding:16px;margin-bottom:16px;
box-shadow:0 1px 4px #0002}h1{font-size:24px;margin:0 0 12px}
h2{font-size:18px;margin:0 0 12px}p.note{color:#52606d}
label{display:block;margin:10px 0 4px;font-weight:bold}
input,select{box-sizing:border-box;width:100%;padding:10px;border:1px solid #b8c2cc;
border-radius:5px;font-size:16px;background:white}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px}
.actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}
textarea{box-sizing:border-box;width:100%;min-height:300px;resize:vertical;
font:13px monospace;padding:10px;background:#101820;color:#d6f5df;border-radius:6px}
.small{font-size:13px;color:#52606d}.status{padding:8px;background:#eef6ff;border-radius:5px}
.ok{color:#146c2e}.bad{color:#a51c30}
</style>
</head>
<body><main class="wrap">
<nav>
<a href="/scan">CC scanner</a>
<a href="/capture">MIDI capture</a>
<a href="/settings">Settings</a>
<a href="/ota">OTA update</a>
</nav>
)HTML";
    return html;
  }

  String pageEnd()
  {
    return F("</main></body></html>");
  }

  String scanPage()
  {
    String html = pageStart("NUX MIDI CC scanner");
    html += R"HTML(
<section>
<h1>NUX MIDI CC scanner</h1>
<p class="note">Отправляются только Control Change. SysEx и опасные системные CC
заблокированы. Минимальная пауза между командами — 250 мс. Проверяйте новые
команды при минимальной громкости.</p>
<div id="status" class="status">Loading...</div>
</section>
<section>
<h2>Параметры перебора</h2>
<div class="grid">
<div><label for="controlPreset">CC parameter</label>
<select id="controlPreset" onchange="presetControl()">
<option value="7">CC 7 — channel volume</option>
<option value="11">CC 11 — expression</option>
<option value="73">CC 73 — legacy NUX value</option>
<option value="81">CC 81 — legacy project volume</option>
<option value="custom">Custom</option>
</select></div>
<div><label for="control">CC number</label>
<input id="control" type="number" min="0" max="127" value="7"></div>
<div><label for="values">Values, comma separated</label>
<input id="values" value="1,32,64,127"></div>
<div><label for="firstChannel">First channel</label>
<input id="firstChannel" type="number" min="1" max="16" value="1"></div>
<div><label for="lastChannel">Last channel</label>
<input id="lastChannel" type="number" min="1" max="16" value="3"></div>
<div><label for="interval">Pause, milliseconds</label>
<input id="interval" type="number" min="250" max="60000" value="500"></div>
</div>
<div class="actions">
<button onclick="startSweep()">Start</button>
<button class="warn" onclick="callApi('/api/scan/pause')">Pause</button>
<button class="danger" onclick="callApi('/api/scan/stop')">Stop</button>
</div>
</section>
<section>
<h2>Одна команда</h2>
<div class="grid">
<div><label for="singleControl">CC number</label>
<input id="singleControl" type="number" min="0" max="127" value="7"></div>
<div><label for="singleValue">Value</label>
<input id="singleValue" type="number" min="0" max="127" value="64"></div>
<div><label for="singleChannel">Channel</label>
<input id="singleChannel" type="number" min="1" max="16" value="1"></div>
</div>
<div class="actions"><button onclick="sendSingle()">Send one CC</button></div>
</section>
<section><h2>Log</h2><textarea id="log" readonly></textarea></section>
<script>
async function callApi(path, data) {
  const options={method:'POST'};
  if(data){options.headers={'Content-Type':'application/x-www-form-urlencoded'};
    options.body=new URLSearchParams(data);}
  const r=await fetch(path,options); const j=await r.json();
  if(!r.ok || j.error) alert(j.error||'Request failed'); refresh();
}
function presetControl(){
  const p=document.getElementById('controlPreset').value;
  if(p!=='custom') document.getElementById('control').value=p;
}
function startSweep(){
  callApi('/api/scan/start',{
    control:document.getElementById('control').value,
    values:document.getElementById('values').value,
    firstChannel:document.getElementById('firstChannel').value,
    lastChannel:document.getElementById('lastChannel').value,
    interval:document.getElementById('interval').value
  });
}
function sendSingle(){
  callApi('/midi/cc',{
    control:document.getElementById('singleControl').value,
    value:document.getElementById('singleValue').value,
    channel:document.getElementById('singleChannel').value
  });
}
async function refresh(){
  const s=await (await fetch('/api/status')).json();
  document.getElementById('status').innerText=
    'BLE: '+(s.connected?'connected':'searching')+
    ' | target: '+s.target+' | sweep: '+s.sweep;
  const l=await (await fetch('/api/logs')).json();
  const box=document.getElementById('log'); box.value=l.text; box.scrollTop=box.scrollHeight;
}
setInterval(refresh,1000); refresh();
</script>)HTML";
    html += pageEnd();
    return html;
  }

  String capturePage()
  {
    String html = pageStart("MIDI capture");
    html += R"HTML(
<section>
<h1>Incoming MIDI capture</h1>
<p class="note">Перехватываются входящие Control Change и Program Change.
SysEx намеренно не регистрируется и не выводится.</p>
<div id="captureStatus" class="status">Loading...</div>
<div class="actions">
<button onclick="capture('/api/capture/start')">Start</button>
<button class="danger" onclick="capture('/api/capture/stop')">Stop</button>
<button class="secondary" onclick="capture('/api/capture/clear')">Clear</button>
</div>
</section>
<section><textarea id="captureLog" readonly></textarea></section>
<script>
async function capture(path){await fetch(path,{method:'POST'});refresh();}
async function refresh(){
 const s=await (await fetch('/api/status')).json();
 document.getElementById('captureStatus').innerText=
   'BLE: '+(s.connected?'connected':'searching')+
   ' | capture: '+(s.capture?'on':'off');
 const l=await (await fetch('/api/capture/log')).json();
 const box=document.getElementById('captureLog');box.value=l.text;
 box.scrollTop=box.scrollHeight;
}
setInterval(refresh,1000);refresh();
</script>)HTML";
    html += pageEnd();
    return html;
  }

  String settingsPage()
  {
    const AppConfig &config = appConfigGet();
    String html = pageStart("Settings");
    html += F("<section><h1>Settings</h1><p class=\"note\">Изменение настроек "
              "сохранит их в NVS и перезапустит ESP32.</p>");
    html += F("<form onsubmit=\"saveSettings(event)\">"
              "<label>NUX BLE name or MAC</label><input id=\"midi\" value=\"");
    html += htmlEscape(config.midiTarget);
    html += F("\"><label>WiFi AP SSID</label><input id=\"ssid\" value=\"");
    html += htmlEscape(config.apSsid);
    html += F("\"><label>WiFi AP password (8+ chars)</label><input "
              "id=\"apPassword\" type=\"password\" value=\"");
    html += htmlEscape(config.apPassword);
    html += F("\"><label>Web login</label><input id=\"webUser\" value=\"");
    html += htmlEscape(config.webUser);
    html += F("\"><label>Web password</label><input id=\"webPassword\" "
              "type=\"password\" value=\"");
    html += htmlEscape(config.webPassword);
    html += F("\"><div class=\"actions\"><button>Save and restart</button></div>"
              "</form></section><script>"
              "async function saveSettings(e){e.preventDefault();"
              "const data={midi:document.getElementById('midi').value,"
              "ssid:document.getElementById('ssid').value,"
              "apPassword:document.getElementById('apPassword').value,"
              "webUser:document.getElementById('webUser').value,"
              "webPassword:document.getElementById('webPassword').value};"
              "const r=await fetch('/api/settings',{method:'POST',headers:"
              "{'Content-Type':'application/x-www-form-urlencoded'},body:"
              "new URLSearchParams(data)});const j=await r.json();"
              "alert(j.message||j.error);}</script>");
    html += pageEnd();
    return html;
  }

  String otaPage()
  {
    String html = pageStart("OTA update");
    html += F("<section><h1>OTA firmware update</h1>"
              "<p class=\"note\">Выберите скомпилированный .bin-файл этой "
              "прошивки. Не отключайте питание до завершения.</p>"
              "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
              "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
              "<div class=\"actions\"><button>Upload firmware</button></div>"
              "</form></section>");
    html += pageEnd();
    return html;
  }

  void handleStatus()
  {
    if (!requireAuth())
      return;

    String body = "{\"connected\":";
    body += midiScannerIsConnected() ? "true" : "false";
    body += ",\"capture\":";
    body += midiScannerCaptureEnabled() ? "true" : "false";
    body += ",\"sweep\":\"";
    body += stateName(sweepScannerState());
    body += "\",\"target\":\"";
    body += jsonEscape(midiScannerTarget());
    body += "\",\"ip\":\"";
    body += WiFi.softAPIP().toString();
    body += "\"}";
    sendJson(body);
  }

  void handleStartSweep()
  {
    if (!requireAuth())
      return;

    int control;
    int firstChannel;
    int lastChannel;
    int interval;
    if (!argumentNumber("control", 0, 127, control) ||
        !argumentNumber("firstChannel", 1, 16, firstChannel) ||
        !argumentNumber("lastChannel", 1, 16, lastChannel) ||
        !argumentNumber("interval", 250, 60000, interval) ||
        !server.hasArg("values")) {
      sendJson("{\"error\":\"Invalid sweep parameters\"}", 400);
      return;
    }

    SweepConfig config = {
      (uint8_t)control,
      (uint8_t)firstChannel,
      (uint8_t)lastChannel,
      (uint16_t)interval,
      server.arg("values")
    };
    String error;
    if (!sweepScannerStart(config, error)) {
      sendJson("{\"error\":\"" + jsonEscape(error) + "\"}", 400);
      return;
    }
    sendJson("{\"ok\":true}");
  }

  void handleSingleCc()
  {
    if (!requireAuth())
      return;

    int control;
    int value;
    int channel;
    if (!argumentNumber("control", 0, 127, control) ||
        !argumentNumber("value", 0, 127, value) ||
        !argumentNumber("channel", 1, 16, channel)) {
      sendJson("{\"error\":\"Invalid CC parameters\"}", 400);
      return;
    }

    if (!sweepScannerControlAllowed((uint8_t)control)) {
      sendJson(
        "{\"error\":\"" +
        jsonEscape(sweepScannerBlockedReason((uint8_t)control)) +
        "\"}",
        400
      );
      return;
    }

    const bool sent = midiScannerSendControlChange(
      (uint8_t)control,
      (uint8_t)value,
      (uint8_t)channel
    );
    sendJson(sent ? "{\"ok\":true}" :
                   "{\"error\":\"BLE-MIDI is not connected\"}",
             sent ? 200 : 409);
  }

  void handleSettings()
  {
    if (!requireAuth())
      return;

    if (!server.hasArg("midi") ||
        !server.hasArg("ssid") ||
        !server.hasArg("apPassword") ||
        !server.hasArg("webUser") ||
        !server.hasArg("webPassword")) {
      sendJson("{\"error\":\"All settings are required\"}", 400);
      return;
    }

    if (!appConfigSave(
          server.arg("midi"),
          server.arg("ssid"),
          server.arg("apPassword"),
          server.arg("webUser"),
          server.arg("webPassword")
        )) {
      sendJson("{\"error\":\"Invalid setting length\"}", 400);
      return;
    }

    restartAt = millis() + 1500;
    sendJson(
      "{\"ok\":true,\"message\":\"Saved to NVS; ESP32 will restart now\"}"
    );
  }

  void handleUpdateUpload()
  {
    if (!server.authenticate(
          appConfigGet().webUser.c_str(),
          appConfigGet().webPassword.c_str()
        )) {
      return;
    }

    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      midiScannerLog("OTA upload started: " + upload.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true))
        midiScannerLog("OTA upload complete");
      else
        Update.printError(Serial);
    }
  }

  void handleUpdateFinished()
  {
    if (!requireAuth())
      return;

    if (Update.hasError()) {
      server.send(500, "text/plain", "OTA update failed");
      return;
    }

    server.send(200, "text/plain", "OTA update complete; restarting");
    restartAt = millis() + 1000;
  }
}

void webInterfaceBegin()
{
  WiFi.mode(WIFI_AP);
  const AppConfig &config = appConfigGet();
  const bool apStarted =
    WiFi.softAP(config.apSsid.c_str(), config.apPassword.c_str());

  server.on("/", HTTP_GET, []() {
    if (requireAuth()) {
      server.sendHeader("Location", "/scan");
      server.send(302, "text/plain", "");
    }
  });
  server.on("/scan", HTTP_GET, []() {
    if (requireAuth())
      server.send(200, "text/html; charset=utf-8", scanPage());
  });
  server.on("/capture", HTTP_GET, []() {
    if (requireAuth())
      server.send(200, "text/html; charset=utf-8", capturePage());
  });
  server.on("/settings", HTTP_GET, []() {
    if (requireAuth())
      server.send(200, "text/html; charset=utf-8", settingsPage());
  });
  server.on("/ota", HTTP_GET, []() {
    if (requireAuth())
      server.send(200, "text/html; charset=utf-8", otaPage());
  });

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/logs", HTTP_GET, []() {
    if (requireAuth())
      sendJson("{\"text\":\"" + jsonEscape(midiScannerEventLog()) + "\"}");
  });
  server.on("/api/capture/log", HTTP_GET, []() {
    if (requireAuth())
      sendJson("{\"text\":\"" + jsonEscape(midiScannerCaptureLog()) + "\"}");
  });
  server.on("/api/scan/start", HTTP_POST, handleStartSweep);
  server.on("/api/scan/pause", HTTP_POST, []() {
    if (requireAuth()) {
      sweepScannerPause();
      sendJson("{\"ok\":true}");
    }
  });
  server.on("/api/scan/stop", HTTP_POST, []() {
    if (requireAuth()) {
      sweepScannerStop();
      sendJson("{\"ok\":true}");
    }
  });
  server.on("/midi/cc", HTTP_POST, handleSingleCc);
  server.on("/api/capture/start", HTTP_POST, []() {
    if (requireAuth()) {
      midiScannerSetCapture(true);
      sendJson("{\"ok\":true}");
    }
  });
  server.on("/api/capture/stop", HTTP_POST, []() {
    if (requireAuth()) {
      midiScannerSetCapture(false);
      sendJson("{\"ok\":true}");
    }
  });
  server.on("/api/capture/clear", HTTP_POST, []() {
    if (requireAuth()) {
      midiScannerClearCapture();
      sendJson("{\"ok\":true}");
    }
  });
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/update", HTTP_POST, handleUpdateFinished, handleUpdateUpload);

  server.begin();

  Serial.println();
  Serial.println("WiFi configuration:");
  Serial.print("  AP started: ");
  Serial.println(apStarted ? "yes" : "no");
  Serial.print("  AP SSID: ");
  Serial.println(config.apSsid);
  Serial.print("  AP password: ");
  Serial.println(config.apPassword);
  Serial.print("  Web login: ");
  Serial.println(config.webUser);
  Serial.print("  Web password: ");
  Serial.println(config.webPassword);
  Serial.print("  Web address: http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("HTTP endpoints:");
  Serial.println("  GET  /scan");
  Serial.println("  GET  /capture");
  Serial.println("  GET  /settings");
  Serial.println("  GET  /ota");
  Serial.println("  POST /midi/cc");
  Serial.println("  POST /api/scan/start");
  Serial.println("  POST /api/scan/pause");
  Serial.println("  POST /api/scan/stop");
  Serial.println("  POST /api/capture/start");
  Serial.println("  POST /api/capture/stop");
  Serial.println("  POST /update");
}

void webInterfaceLoop()
{
  server.handleClient();
  if (restartAt != 0 && millis() >= restartAt) {
    Serial.println("Restarting ESP32");
    delay(100);
    ESP.restart();
  }
}