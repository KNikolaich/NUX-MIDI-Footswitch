#pragma once

#include <Arduino.h>

struct AppConfig
{
  String midiTarget;
  String apSsid;
  String apPassword;
  String webUser;
  String webPassword;
};

void appConfigBegin();
const AppConfig &appConfigGet();
bool appConfigSave(
  const String &midiTarget,
  const String &apSsid,
  const String &apPassword,
  const String &webUser,
  const String &webPassword
);