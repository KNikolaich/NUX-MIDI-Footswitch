---
name: ESP32 validation
description: Arduino CLI availability and build validation limits in this Replit container.
---

The Arduino CLI PATH entry is a symlink to a Nix-generated `bwrap` launcher that
fails in this nested sandbox. Its init script identifies the real CLI binary; invoke
that binary directly. ESP32 `esptool.py` uses `/usr/bin/env python`, but the base
Python lacks `pyserial`. Replit's Python package installer places it in `.pythonlibs`,
so the compile process needs that directory on `PYTHONPATH`. In a non-Python repl,
that installer can also scaffold root Python files and alter `.replit`; remove those
unrequested scaffolds and restore the original config.

**Why:** Firmware builds depend on downloaded Python tools as well as the Arduino
compiler, and Replit's normal CLI and Python package launchers isolate those tools
differently from a system-wide installation.

**How to apply:** Resolve the CLI symlink and inspect its init script before retrying
the wrapper. After making `pyserial` available, set `PYTHONPATH` to the installed
`.pythonlibs/lib/pythonX.Y/site-packages` directory for `arduino-cli compile`.