#include "AppConfig.h"

#include <Preferences.h>

namespace
{
  Preferences preferences;
  AppConfig config;

  const char *DEFAULT_MIDI_TARGET = "cb:4e:fd:a3:6c:1b";
  const char *DEFAULT_AP_SSID = "NUX-Scanner";
  const char *DEFAULT_AP_PASSWORD = "nux12345";
  const char *DEFAULT_WEB_USER = "admin";
  const char *DEFAULT_WEB_PASSWORD = "nux12345";

  bool validText(const String &value, size_t minLength, size_t maxLength)
  {
    return value.length() >= minLength && value.length() <= maxLength;
  }
}

void appConfigBegin()
{
  preferences.begin("nuxscanner", false);

  config.midiTarget = preferences.getString("midi", DEFAULT_MIDI_TARGET);
  config.apSsid = preferences.getString("ssid", DEFAULT_AP_SSID);
  config.apPassword = preferences.getString("ap-pass", DEFAULT_AP_PASSWORD);
  config.webUser = preferences.getString("web-user", DEFAULT_WEB_USER);
  config.webPassword =
    preferences.getString("web-pass", DEFAULT_WEB_PASSWORD);
}

const AppConfig &appConfigGet()
{
  return config;
}

bool appConfigSave(
  const String &midiTarget,
  const String &apSsid,
  const String &apPassword,
  const String &webUser,
  const String &webPassword
)
{
  if (!validText(midiTarget, 1, 22) ||
      !validText(apSsid, 1, 32) ||
      !validText(apPassword, 8, 63) ||
      !validText(webUser, 1, 32) ||
      !validText(webPassword, 1, 63)) {
    return false;
  }

  preferences.putString("midi", midiTarget);
  preferences.putString("ssid", apSsid);
  preferences.putString("ap-pass", apPassword);
  preferences.putString("web-user", webUser);
  preferences.putString("web-pass", webPassword);

  config.midiTarget = midiTarget;
  config.apSsid = apSsid;
  config.apPassword = apPassword;
  config.webUser = webUser;
  config.webPassword = webPassword;
  return true;
}