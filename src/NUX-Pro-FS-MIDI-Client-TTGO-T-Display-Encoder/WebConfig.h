#pragma once

#include "DeviceSettings.h"

void webConfigBegin(DeviceSettings &settings);
void webConfigHandleClient();
bool webConfigRestartRequested();
