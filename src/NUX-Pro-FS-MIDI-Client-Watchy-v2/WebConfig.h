#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "DeviceSettings.h"

class WebConfig
{
public:
  WebConfig();

  void begin(DeviceSettingsStore &settingsStore);
  void togglePortal();
  void loop();

  bool portalActive() const;
  bool restartRequested() const;

private:
  void registerRoutes();
  bool startPortal();
  void stopPortal();
  bool requireAuthentication();
  void touchActivity();

  String settingsPage() const;
  void handleRoot();
  void handleSettings();
  void handleSave();
  void handleUpdatePage();
  void handleUpdateUpload();
  void handleUpdateComplete();

  WebServer _server;
  DeviceSettingsStore *_settingsStore = nullptr;
  bool _routesRegistered = false;
  bool _portalActive = false;
  bool _updateStarted = false;
  bool _updateSucceeded = false;
  uint32_t _lastActivityAt = 0;
  uint32_t _restartAt = 0;
};