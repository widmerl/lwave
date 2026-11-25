# L-wave

Firmware for the L-wave: a Tonnetz-layout polyphonic synth with 48 Hall-effect keys, built on a Teensy 4.1.

- **Build the Teensy firmware:** open `firmware/` in VS Code with the PlatformIO extension, then *Build* and *Upload*.
- **Run the tests and render demo sounds on a PC:** see `docs/REFERENCE.md`, section 1.
- **How everything works, plus every function:** `docs/REFERENCE.md`.

Status: version 0.1.0. It compiles for the Teensy 4.1, passes 72 host tests and the simulator test, and runs on the first hardware build.

**Try it without hardware:** open `simulator/lwave-sim.html` in Chrome or Edge. The same page has a hardware monitor that shows what the real synth reads over USB.

At power-up: hold **C4 (key D5)** for calibration, or **D3 (key F4)** for debug mode. The serial console (115200 baud) accepts `help`.
