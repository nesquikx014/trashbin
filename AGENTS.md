# Repository Guidelines

## Project Structure & Module Organization
- `trashbin.ino`: Main Arduino sketch controlling sensors, servo, VS1053 Music Maker shield, and idle/random audio playback.
- `argend.md`: Functional walkthrough of the sketch.
- `README.md`: User-facing overview (keep aligned with this guide).
- `journals-*.md`: Running notes; do not edit unless explicitly assigned.

## Build, Test, and Development Commands
- `arduino-cli compile --fqbn adafruit:avr:feather32u4 trashbin.ino` – Compile the sketch for the target board; adjust FQBN as needed (see `arduino-cli board list`).
- `arduino-cli upload -p /dev/ttyACM0 --fqbn adafruit:avr:feather32u4 trashbin.ino` – Upload to the connected board; replace port/FQBN for your environment.
- `arduino-cli monitor -p /dev/ttyACM0 -b 9600` – Open serial monitor at 9600 baud for live diagnostics.

## Coding Style & Naming Conventions
- Follow Arduino C++ style: two-space indentation, braces on new lines, `camelCase` for functions, `SCREAMING_SNAKE_CASE` for constants.
- Keep helper functions `static` when they remain in the module.
- Use `F("...")` for flash-stored strings when printing to conserve SRAM.
- Document non-obvious logic with brief inline comments (prefer single-line `//`).

## Testing Guidelines
- Hardware behavior is validated manually; log outputs are critical.
- Enable `debugMode` in `trashbin.ino` while developing to access sensor readouts and serial triggers.
- After every change, verify: servo actuation, trigger responsiveness (hand test + serial empty line), idle playback, and random playlist rotation.
- Capture serial snapshots when reporting bugs; include timestamps and key log lines.

## Commit & Pull Request Guidelines
- Craft commits around logical units; message format `<short imperative summary> (module)` is preferred, e.g., `Tune trigger hysteresis (trashbin.ino)`.
- Reference issue IDs in the body when applicable (`Refs #12`).
- PRs should include: summary of changes, testing evidence (serial log excerpt), and any hardware considerations (power, wiring tweaks).
- Request review from hardware maintainers before merging changes that affect servo control, sensor thresholds, or audio timing.
