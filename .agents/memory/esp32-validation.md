---
name: ESP32 validation
description: Arduino CLI availability and build validation limits in this Replit container.
---

Arduino CLI 1.2.2 is installed as a Nix system dependency. Its normal PATH launcher
creates an FHS environment through nested `bwrap`, which fails inside this Replit
sandbox with an "Unexpected capabilities but not setuid" error. The underlying
Nix-store executable runs when invoked directly.

An ESP32 core 2.0.17 install reported success, but a later `arduino-cli core list`
showed no platforms installed, so no firmware build was confirmed. The Library
Manager index also no longer offers BLE-MIDI 1.4.3; it offers newer releases, which
may not match existing sketches.

**Why:** The repository contains several board-specific sketches whose BLE-MIDI and
Arduino-core APIs depend on the selected board package and library versions, while
the package launcher and installation state behave differently in this sandbox.

**How to apply:** Invoke the underlying CLI binary if the PATH launcher fails, verify
`core list` and required libraries after installation, and do not claim a build passed
until the compile command succeeds.