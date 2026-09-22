---
name: ESP32 validation
description: The workspace container lacks the Arduino ESP32 build toolchain.
---

Embedded Arduino sketches cannot be compiled or uploaded from this workspace because
`arduino-cli` and PlatformIO are not installed. Static checks such as `git diff --check`,
delimiter checks, and API/source inspection are possible, but final validation must be
done by opening the sketch in Arduino IDE with the matching ESP32 core and libraries.

**Why:** The repository contains several board-specific sketches whose BLE-MIDI and
Arduino-core APIs depend on the user's local board package and library versions.

**How to apply:** Do not claim an embedded build passed based only on container checks;
report the limitation and give the user the exact sketch folder and required libraries.