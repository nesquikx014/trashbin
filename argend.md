# Trashbin Firmware Walkthrough

This document explains every major subsystem in `trashbin.ino`, how they interact, and the key constants you can tweak. Think of it as a compact field-guide for the sketch.

---

## 1. Hardware Setup

| Subsystem | Pins | Notes |
|-----------|------|-------|
| VS1053 Music Maker | `MP3_CS=7`, `MP3_DCS=6`, `MP3_DREQ=3`, `SD_CS=4` | Handles MP3 playback and SD access. |
| Ultrasonic Sensors | Sensor A: `TRIG1=A2`, `ECHO1=A3`<br>Sensor B: `TRIG2=A5`, `ECHO2=A4` | Two HC-SR04 modules provide dual-angle detection. |
| Servo | `SERVO_PIN=9` | Physically “presses” whatever mechanism the bin uses. |
| External Trigger Output | `OUT_PIN=A1`, `OUT_PIN_MIRROR=A0` | 1‑second HIGH pulse once audio actually starts (mirrored output). |
| Onboard LED | `LED_BUILTIN` | Mirrors the confirmed trigger state. |

Startup initializes all pins, attaches the servo, boots the VS1053 + SD card, and then searches the SD root for key audio files.

---

## 2. Core Constants

| Constant | Purpose | Default |
|----------|---------|---------|
| `TRIGGER_CM` | Distance threshold (cm) to consider an object “close”. | `44.0f` |
| `MAX_VALID_CM` | Rejects out-of-range or garbage ultrasonic readings. | `44.0f` |
| `MUTE_INTERVAL_MS` | Minimum spacing between trigger print/log entries. | `200 ms` |
| `PING_GAP_MS` | Delay between HC-SR04 polls. | `100 ms` |
| `TRIGGER_CONFIRMATIONS` | Consecutive close readings needed to confirm an edge. | `5` |
| `TRIGGER_HOLD_MS` | Additional “must stay in range” time. `0` ⇒ none. | `0 ms` |
| `SERVO_HOLD_MS` | How long the servo stays at the “pressed” angle. | `1 s` |
| `SERVO_COOLDOWN_MS` | Lockout after a servo press before next trigger. | `6 s` |
| `IDLE_INTERVAL_MS` | Minimum gap before replaying `IDLE.MP3`. | `10 s` |
| `OUT_PULSE_MS` | Length of external trigger HIGH pulse. | `1 s` |
| `PULSE_ARM_TIMEOUT_MS` | How long to wait for playback before cancelling OUT pulse. | `300 ms` |
| `MAX_RANDOM_TRACKS` | Maximum non-idle random MP3 entries cached from SD. | `32` |
| `debugMode` | Enables manual serial trigger when `true`. | `false` |

All constants live near the top of `trashbin.ino` for quick tuning.

---

## 3. Audio Files & Playback Flow

### Key Files
* `TRIGGER.MP3` – optional special track for a trigger event.
* `IDLE.MP3` – loops in the background whenever no other audio plays.
* Any other `.MP3` – part of the random playlist for fallback triggers.

### Initialization
1. Check SD for `TRIGGER.MP3` and `IDLE.MP3`.
2. Build the random playlist of every other MP3 (up to 32 entries).
3. Start `IDLE.MP3` immediately if present.

### Idle Service
`serviceIdlePlayback()` runs each loop:
* Tracks whether idle audio is currently playing.
* Waits `IDLE_INTERVAL_MS` after the last start before relaunching.
* Stops the idle loop whenever an actual trigger path wants exclusive playback.
* Logs `Idle: playback completed` when the clip hits EOF naturally.

### Trigger Playback
`playTriggerOrRandom()`:
1. Calls `suspendIdlePlaybackForEvent()` to stop `IDLE.MP3`.
2. Tries `TRIGGER.MP3` (if it exists), otherwise picks the next unused random track.
3. Arms the OUT pins so they pulse for exactly 1 second once playback actually begins (`servicePulseArm()` manages this).

Random playback ensures each song is heard once before repeating:
* `refreshRandomTrackList()` rescans SD and fills `randomTrackUsed[]`.
* `playRandomMp3()` picks an unused entry, marks it used, and resets the pool once everything has played.

---

## 4. Trigger Detection & Servo Control

### Ultrasonic Sampling
`handleSensors()` performs every loop:
1. Poll both sensors (spaced `PING_GAP_MS` apart).
2. Convert durations to centimeters; reject out-of-range readings.
3. Maintain `inRangeStreak` – consecutive “object is close” readings.
4. Once the streak meets `TRIGGER_CONFIRMATIONS` it explores the trigger path.

### Servo & Cooldown Gate
* `triggerServoMove()` returns `true` only when the servo actually moves (cooldown satisfied); otherwise it prints the remaining cooldown and returns `false`.
* Playback is only launched if `triggerServoMove()` succeeds. This prevents rapid repeat triggers while the servo is still resetting.

### Serial Trigger (Debug)
`handleSerialTrigger()` checks the USB serial buffer. If `debugMode == true` and an empty line arrives, it behaves like a sensor trigger and respects the same servo gating.

---

## 5. OUT Pulse Handling

Two helper functions manage the external trigger line:
* `armOutPulseOnPlaybackStart()` – called when playback begins, arming a pulse.
* `servicePulseArm()` – waits until VS1053 reports `playingMusic == true`, then drives both OUT pins HIGH and starts the 1 s timer.
* `serviceOutPulse()` – turns the outputs LOW after the timer expires.

This combination guarantees the external device is only pulsed once audio is genuinely streaming.

---

## 6. Utility & Support Functions

* `measureDistanceCm()` – safely drives an HC-SR04 ping, constraining answers to useful ranges.
* `resetRandomPlaylistUsage()` – clears usage flags when every track has been played.
* `startIdlePlayback()` / `suspendIdlePlaybackForEvent()` – simple wrappers to encapsulate idle state and enforce the “no idle during events” rule.
* `serviceServo()` – releases the servo back to its rest angle after `SERVO_HOLD_MS`.

---

## 7. Main Loop Summary

```mermaid
flowchart TD
  A[loop()] --> B(handleSensors)
  A --> C(servicePulseArm)
  A --> D(serviceOutPulse)
  A --> E(serviceServo)
  A --> F(handleSerialTrigger)
  A --> G(serviceIdlePlayback)
```

Execution order matters: we look for triggers first, then manage pulses, reset the servo, check serial, and finally tend the idle audio. A short `delay(10)` caps the loop frequency and eases sensor timing.

---

## 8. Tuning Tips

* **Too sensitive?** Increase `TRIGGER_CONFIRMATIONS`, reduce `TRIGGER_CM`, or reposition sensors to avoid echoes.
* **Missing quick drops?** Lower `TRIGGER_CONFIRMATIONS` or shorten `PING_GAP_MS`, but watch for noise. `TRIGGER_HOLD_MS` should remain `0` for fast-moving objects.
* **Idle track restarts too often?** Raise `IDLE_INTERVAL_MS` or verify that triggers aren’t constantly firing.
* **Need more random tracks?** Increase `MAX_RANDOM_TRACKS` if RAM allows and expand the `randomTrackUsed[]` array accordingly.
* **Manual testing from Serial Monitor?** Set `debugMode = true` near the top of the sketch.

---

## 9. Typical Serial Log Flow

```
Ready. Pulsing OUT only after audio playback actually starts.
Servo initialized to 50°
Music Maker ready.
TRIGGER.MP3 not found. Random MP3 fallback enabled.
IDLE.MP3 detected. Idle playback enabled.
Random pool size: 6
Idle: playing IDLE.MP3
TRIGGERED (edge)
Servo: move to 5° (pressed)
Idle: stopping IDLE.MP3 for trigger playback
Random MP3 selected: BARK1.MP3
Playback started → OUT_PIN pulse: ON for 1s
...
Idle: playback completed
Idle: trigger playback finished, resuming idle mode
Idle: playing IDLE.MP3
```

Use this to confirm the system’s behavior matches expectations after you tweak any constants.

---

Questions or ideas for tweaks? Drop them in the main README or add inline comments where the change applies. This guide should give you the context needed to iterate confidently. Happy hacking!
