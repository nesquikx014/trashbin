#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <Servo.h>
#include <string.h>

// ------------ Output ------------
#define OUT_PIN A1              // ESP32 trigger output (primary)
#define OUT_PIN_MIRROR A0       // ESP32 trigger output (mirrored)

// ------------ Pins (Music Maker Shield defaults) ------------
#define MP3_CS     7
#define MP3_DCS    6
#define MP3_DREQ   3
#define SD_CS      4

// ------------ Ultrasonic pins ------------
#define TRIG1_PIN  A2
#define ECHO1_PIN  A3
#define TRIG2_PIN  A5
#define ECHO2_PIN  A4

// ------------ Servo ------------
#define SERVO_PIN 9
#define SERVO_HOLD_MS 1000UL
#define SERVO_MIN     5
#define SERVO_MAX     30
#define SERVO_COOLDOWN_MS 0UL  // Temporarily disable cooldown for rapid triggers


Servo triggerServo;
bool servoActive = false;
unsigned long servoStartMs = 0;
unsigned long lastServoMoveMs = 0;

// ------------ Behavior ------------
#define MUTE_INTERVAL_MS 200UL
#define TRIGGER_CM     20.0f
#define MAX_VALID_CM   20.0f
#define TIMEOUT_US     6000UL
#define PING_GAP_MS  100UL


// OUT_PIN pulse on music trigger
#define OUT_PULSE_MS          1000UL
#define PULSE_ARM_TIMEOUT_MS   300UL
#define TRIGGER_CONFIRMATIONS     5
#define TRIGGER_HOLD_MS          0UL
#define TRIGGER_RELEASE_MARGIN_CM 5.0f
#define TRIGGER_RELEASE_CONFIRMATIONS 3
#define MAX_RANDOM_TRACKS        32

// Debug control
bool debugMode = false;

// Sensor-trigger single file
const char *TRIGGER_FILE = "TRIGGER.MP3";
bool triggerFileAvailable = false;

// Idle/background audio
const char *IDLE_FILE = "IDLE.MP3";
const unsigned long IDLE_INTERVAL_MS = 10000UL;
bool idleFileAvailable = false;
bool idlePlaying = false;
bool idleSuppressed = false;
unsigned long lastIdleStartMs = 0;

// Random playback state (shuffled per boot, sequential playback)
char randomTracks[MAX_RANDOM_TRACKS][13];
uint8_t randomTrackCount = 0;
uint8_t nextRandomIndex = 0;

bool isPlayableMp3(const char *name) {
  if (!name) return false;

  char upper[13] = {0};
  uint8_t len = 0;
  for (; name[len] != '\0' && len < 12; ++len) {
    char c = name[len];
    if (c >= 'a' && c <= 'z') c -= 32;
    upper[len] = c;
  }
  upper[len] = '\0';

  if (len < 4) return false;
  if (upper[len - 4] != '.' || upper[len - 3] != 'M' || upper[len - 2] != 'P' || upper[len - 1] != '3') {
    return false;
  }
  if (strcmp(upper, TRIGGER_FILE) == 0) return false;
  if (strcmp(upper, IDLE_FILE) == 0) return false;
  return true;
}

// VS1053 player
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(
  MP3_CS, MP3_DCS, MP3_DREQ, SD_CS
);

unsigned long lastTriggerPrintMs = 0;

// ---- Helper: measure one HC-SR04 safely ----
float measureDistanceCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, TIMEOUT_US);
  if (duration == 0) return -1.0f;

  float cm = duration * 0.034f / 2.0f;
  if (cm <= 0.0f || cm > MAX_VALID_CM) return -1.0f;
  if ((int)cm == 794) return -1.0f;
  return cm;
}

// ---- OUT_PIN control (flipped: idle LOW) ----
bool outPinState = LOW;
bool outPulseActive = false;
unsigned long outPulseStartMs = 0;

// ---- Arm pulse only after playback actually starts ----
bool pulseArmed = false;
unsigned long pulseArmStartMs = 0;

void armOutPulseOnPlaybackStart() {
  pulseArmed = true;
  pulseArmStartMs = millis();
  Serial.println(F("OUT pulse armed: waiting for playback start..."));
}

void servicePulseArm() {
  if (!pulseArmed) return;

  unsigned long now = millis();
  if (musicPlayer.playingMusic) {
    digitalWrite(OUT_PIN, HIGH);
    digitalWrite(OUT_PIN_MIRROR, HIGH);
    outPinState = HIGH;
    outPulseActive = true;
    outPulseStartMs = now;
    pulseArmed = false;
    Serial.println(F("Playback started → OUT_PIN pulse: ON for 1s"));
  } else if (now - pulseArmStartMs >= PULSE_ARM_TIMEOUT_MS) {
    pulseArmed = false;
    Serial.println(F("Playback did not start in time → skipping OUT pulse"));
  }
}

void serviceOutPulse() {
  if (outPulseActive && (millis() - outPulseStartMs >= OUT_PULSE_MS)) {
    digitalWrite(OUT_PIN, LOW);
    digitalWrite(OUT_PIN_MIRROR, LOW);
    outPinState = LOW;
    outPulseActive = false;
    Serial.println(F("OUT_PIN pulse: back OFF"));
  }
}

void suspendIdlePlaybackForEvent() {
  if (idlePlaying) {
    Serial.println(F("Idle: stopping IDLE.MP3 for trigger playback"));
    musicPlayer.stopPlaying();
    idlePlaying = false;
  }
  idleSuppressed = true;
  lastIdleStartMs = millis();
}

bool startIdlePlayback() {
  if (!idleFileAvailable) return false;
  if (musicPlayer.playingMusic) return false;

  if (musicPlayer.startPlayingFile(IDLE_FILE)) {
    idlePlaying = true;
    lastIdleStartMs = millis();
    Serial.println(F("Idle: playing IDLE.MP3"));
    return true;
  }

  idleFileAvailable = false;
  Serial.println(F("Idle: failed to play IDLE.MP3; disabling idle mode."));
  return false;
}

void serviceIdlePlayback() {
  bool playing = musicPlayer.playingMusic;

  if (!playing && idlePlaying) {
    idlePlaying = false;
    Serial.println(F("Idle: playback completed"));
  }

  if (!playing && idleSuppressed && !pulseArmed) {
    idleSuppressed = false;
    Serial.println(F("Idle: trigger playback finished, resuming idle mode"));
  }

  if (!playing && !idleSuppressed && idleFileAvailable && !idlePlaying) {
    unsigned long now = millis();
    if ((now - lastIdleStartMs) >= IDLE_INTERVAL_MS) {
      startIdlePlayback();
    }
  }
}

void refreshRandomTrackList() {
  File dir = SD.open("/", FILE_READ);
  if (!dir) {
    Serial.println(F("Failed to open SD root while building playlist. Retrying..."));

    if (!SD.begin(SD_CS)) {
      Serial.println(F("SD reinit failed; leaving playlist unchanged."));
      randomTrackCount = 0;
      return;
    }

    dir = SD.open("/", FILE_READ);
    if (!dir) {
      Serial.println(F("Retry failed; random playlist unavailable."));
      randomTrackCount = 0;
      return;
    }
  }

  uint8_t count = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory() && isPlayableMp3(entry.name())) {
      if (count < MAX_RANDOM_TRACKS) {
        const char *entryName = entry.name();
        uint8_t j = 0;
        for (; entryName[j] != '\0' && j < 12; ++j) {
          randomTracks[count][j] = entryName[j];
        }
        randomTracks[count][j] = '\0';
        count++;
      } else {
        Serial.println(F("Random playlist full; ignoring extra tracks."));
      }
    }
    entry.close();
  }
  dir.close();

  randomTrackCount = count;
  nextRandomIndex = 0;

  if (randomTrackCount > 1) {
    for (int i = randomTrackCount - 1; i > 0; --i) {
      int j = random(i + 1);
      if (j != i) {
        char temp[13];
        memcpy(temp, randomTracks[i], sizeof(temp));
        memcpy(randomTracks[i], randomTracks[j], sizeof(temp));
        memcpy(randomTracks[j], temp, sizeof(temp));
      }
    }
  }

  if (randomTrackCount > 0) {
    Serial.print(F("Random pool size: "));
    Serial.println(randomTrackCount);
  } else {
    Serial.println(F("Random pool empty."));
  }
}

bool playRandomMp3() {
  if (randomTrackCount == 0) {
    refreshRandomTrackList();
  }

  if (randomTrackCount == 0) {
    Serial.println(F("No alternate MP3 files found on SD."));
    return false;
  }

  const char *chosenName = randomTracks[nextRandomIndex];
  nextRandomIndex++;
  if (nextRandomIndex >= randomTrackCount) {
    nextRandomIndex = 0;
  }

  if (chosenName[0] == '\0') {
    Serial.println(F("Random playlist entry empty; refreshing list."));
    refreshRandomTrackList();
    if (randomTrackCount == 0) return false;
    chosenName = randomTracks[nextRandomIndex];
    nextRandomIndex = (nextRandomIndex + 1) % randomTrackCount;
  }

  Serial.print(F("Random MP3 selected: "));
  Serial.println(chosenName);

  if (!musicPlayer.startPlayingFile(chosenName)) {
    Serial.println(F("Failed to start selected random MP3."));
    return false;
  }

  Serial.println(F("Playing random MP3 fallback."));
  return true;
}

bool playTriggerOrRandom() {
  suspendIdlePlaybackForEvent();

  if (triggerFileAvailable) {
    if (musicPlayer.startPlayingFile(TRIGGER_FILE)) {
      Serial.println(F("Starting TRIGGER.MP3"));
      armOutPulseOnPlaybackStart();
      return true;
    }
    Serial.println(F("Failed to play TRIGGER.MP3, falling back to random MP3."));
    triggerFileAvailable = false;
  }

  if (playRandomMp3()) {
    armOutPulseOnPlaybackStart();
    return true;
  }

  idleSuppressed = false;
  return false;
}

// ---- Servo helpers ----
bool triggerServoMove() {
  unsigned long now = millis();

  // Enforce cooldown
  if (now - lastServoMoveMs < SERVO_COOLDOWN_MS) {
    unsigned long remain = SERVO_COOLDOWN_MS - (now - lastServoMoveMs);
    Serial.print(F("Servo: cooldown active, "));
    Serial.print(remain);
    Serial.println(F(" ms remaining"));
    return false; // block this trigger
  }

  // Allow the move and start a new cooldown window
  lastServoMoveMs = now;

  // Move to "pressed" position
  triggerServo.write(SERVO_MIN);
  servoActive = true;
  servoStartMs = now;
  Serial.println("Servo: move to " + String(SERVO_MIN) + "° (pressed)");

  return true;
}

void serviceServo() {
  // After holding for SERVO_HOLD_MS, return to "rest" position
  if (servoActive && (millis() - servoStartMs >= SERVO_HOLD_MS)) {
    triggerServo.write(SERVO_MAX);
    servoActive = false;
    Serial.println("Servo: back to " + String(SERVO_MAX) + "° (rest)");
  }
}

void setup() {
  randomSeed(analogRead(A3) ^ analogRead(A4) ^ analogRead(A5));

  // OUT pin + LED
  pinMode(OUT_PIN, OUTPUT);
  pinMode(OUT_PIN_MIRROR, OUTPUT);
  digitalWrite(OUT_PIN, outPinState);   // start OFF (LOW)
  digitalWrite(OUT_PIN_MIRROR, outPinState);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Serial
  Serial.begin(9600);
  Serial.println(F("Ready. Pulsing OUT only after audio playback actually starts."));

  // Ultrasonic
  pinMode(TRIG1_PIN, OUTPUT);
  pinMode(ECHO1_PIN, INPUT);
  pinMode(TRIG2_PIN, OUTPUT);
  pinMode(ECHO2_PIN, INPUT);

  // Servo
  // Note: On ATmega328P boards the Servo library uses Timer1.
  // Using D10 is fine here. It also keeps hardware SS as OUTPUT for SPI stability.
  triggerServo.attach(SERVO_PIN);
  triggerServo.write(50);  // Start at 50° on boot
  Serial.println(F("Servo initialized to 50°"));


  // Audio
  if (!musicPlayer.begin()) {
    Serial.println(F("VS1053 not found. Check wiring/shield."));
    while (1) { delay(10); }
  }
  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD init failed. Check card/format/CS pin."));
    while (1) { delay(10); }
  }
  musicPlayer.setVolume(20, 20);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);
  Serial.println(F("Music Maker ready."));

  triggerFileAvailable = SD.exists(TRIGGER_FILE);
  if (triggerFileAvailable) {
    Serial.println(F("TRIGGER.MP3 detected on SD."));
  } else {
    Serial.println(F("TRIGGER.MP3 not found. Random MP3 fallback enabled."));
  }

  idleFileAvailable = SD.exists(IDLE_FILE);
  if (idleFileAvailable) {
    Serial.println(F("IDLE.MP3 detected. Idle playback enabled."));
  } else {
    Serial.println(F("IDLE.MP3 not found. Idle playback disabled."));
  }

  refreshRandomTrackList();

  if (idleFileAvailable) {
    lastIdleStartMs = millis() - IDLE_INTERVAL_MS;  // allow immediate first play
    startIdlePlayback();
  }
}

void handleSensors() {
  static unsigned long lastPingMs = 0;
  static float d1 = -1.0f, d2 = -1.0f;
  static uint8_t inRangeStreak = 0;
  static unsigned long inRangeStartMs = 0;
  static uint8_t outOfRangeStreak = 0;
  static bool wasInRange = false;

  unsigned long now = millis();
  if (now - lastPingMs >= PING_GAP_MS) {
    d1 = measureDistanceCm(TRIG1_PIN, ECHO1_PIN);
    delayMicroseconds(1500);
    d2 = measureDistanceCm(TRIG2_PIN, ECHO2_PIN);
    lastPingMs = now;
  }

  bool inRange = ( (d1 > 0 && d1 < TRIGGER_CM) || (d2 > 0 && d2 < TRIGGER_CM) );
  float releaseThreshold = TRIGGER_CM + TRIGGER_RELEASE_MARGIN_CM;
  bool withinReleaseBand = (
    (d1 > 0 && d1 < releaseThreshold) ||
    (d2 > 0 && d2 < releaseThreshold)
  );
  bool fullyOut = !withinReleaseBand;

  if (inRange) {
    if (inRangeStreak < TRIGGER_CONFIRMATIONS) {
      inRangeStreak++;
    }
    if (inRangeStartMs == 0) {
      inRangeStartMs = now;
    }
    outOfRangeStreak = 0;
  } else {
    inRangeStreak = 0;
    inRangeStartMs = 0;
    if (fullyOut) {
      if (outOfRangeStreak < TRIGGER_RELEASE_CONFIRMATIONS) {
        outOfRangeStreak++;
      }
    } else {
      outOfRangeStreak = 0;
    }
  }

  bool holdSatisfied = (TRIGGER_HOLD_MS == 0UL) ? true : ((inRangeStartMs != 0) && ((now - inRangeStartMs) >= TRIGGER_HOLD_MS));
  bool confirmedRange = (inRangeStreak >= TRIGGER_CONFIRMATIONS) && holdSatisfied;
  bool nextWasInRange = wasInRange;

  if (confirmedRange && !wasInRange && (now - lastTriggerPrintMs >= MUTE_INTERVAL_MS)) {
    Serial.println(F("TRIGGERED (edge)"));
    digitalWrite(LED_BUILTIN, HIGH);

    // Kick the servo immediately on edge
    bool servoTriggered = triggerServoMove();

    if (servoTriggered && (!musicPlayer.playingMusic || idlePlaying)) {
      if (!playTriggerOrRandom()) {
        Serial.println(F("No audio track could be started."));
      }
    } else if (!servoTriggered) {
      // Servo still cooling; skip audio to avoid rapid retriggers
    } else {
      Serial.println(F("Already playing, skipping OUT pulse to keep sync"));
    }
    lastTriggerPrintMs = now;
    nextWasInRange = true;
  } else if (!confirmedRange && wasInRange) {
    if (outOfRangeStreak >= TRIGGER_RELEASE_CONFIRMATIONS) {
      nextWasInRange = false;
    }
  }

  digitalWrite(LED_BUILTIN, nextWasInRange ? HIGH : LOW);
  wasInRange = nextWasInRange;

  if (!confirmedRange && !wasInRange) {
    outOfRangeStreak = 0;  // reset streak once fully disarmed
  }
}

void handleSerialTrigger() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');  // read until newline

    // Trim whitespace (useful if Serial Monitor sends \r\n)
    input.trim();

    // If the input line is empty, trigger manually
    if (input.length() == 0 && debugMode) {
      Serial.println(F("Serial trigger received (empty line)"));

      // Kick the servo
      bool servoTriggered = triggerServoMove();

      // Play TRIGGER.MP3 if not already playing
      if (servoTriggered && (!musicPlayer.playingMusic || idlePlaying)) {
        if (!playTriggerOrRandom()) {
          Serial.println(F("Serial trigger: no audio track available."));
        }
      } else if (!servoTriggered) {
        // Servo still cooling; skip audio
      } else {
        Serial.println(F("Already playing, skipping OUT pulse to keep sync"));
      }
    }
  }
}


void loop() {
  handleSensors();              // ultrasonic triggers
  servicePulseArm();            // fire OUT pulse when playback actually starts
  serviceOutPulse();            // end the 1s OUT pulse
  serviceServo();               // return servo to 0° after hold
  handleSerialTrigger();        // listen for empty-line triggers
  serviceIdlePlayback();        // keep IDLE.MP3 running when nothing else is
  delay(10);
}
