/*
 * Axworthy Ghost Pulley System — Block 2 (XIAO SAMD21 port)
 * Based on Block 1 firmware v1.3. Same SAMD21 chip, so the TC4 timer motion
 * engine and FlashStorage are unchanged.
 *
 * Hardware: Seeeduino XIAO SAMD21 + A4988 + NEMA 17
 *           + SSD1309 2.42" OLED (I2C) + EC11 Rotary Encoder
 *           + DS3231 RTC (I2C, address 0x68)
 *
 * Differences from Block 1:
 *   - Controller: XIAO SAMD21 (was MKR Zero) — pins remapped (see below)
 *   - Microstepping (MS1/MS2/MS3): set by an on-board DIP switch, NOT the MCU.
 *     Firmware no longer drives the MS pins. DIP = 1/8 (H/H/L), which the
 *     3200 steps/rev math (STEPS_PER_INCH 339.4) assumes.
 *   - Fan circuit removed entirely (no FAN pin, no fan logic).
 *
 * Pin map (XIAO SAMD21):
 *   D0=STEP  D1=DIR  D2=ENABLE  D3=ENC_CLK(interrupt)
 *   D4=SDA   D5=SCL  D6=ENC_DT  D7=ENC_SW  D8=LIMIT
 *   D9=AUX1 (spare)  D10=AUX2 (spare)
 *
 * Required Libraries (install via Tools > Manage Libraries):
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - FlashStorage by cmaglie
 *   - RTClib by Adafruit
 *
 * Board: install "Seeed SAMD Boards" and select "Seeeduino XIAO".
 *
 * STATUS: Public Release — tested and verified on the real Block 2 PCB.
 *
 * Changes since initial release:
 *   - SCHEDULE runs now re-home against the limit switch at the START and END
 *     of each scheduled window (corrects wind drift while idle; prevents a
 *     crash into the pulley end). Continuous mode unchanged.
 *   - Added a manual "Home" item to the Setup menu.
 *   - Setup menu reordered; "Learn Positions" moved to the bottom to avoid
 *     accidental presses.
 */

//#define NO_OLED   // uncomment to use Serial Monitor instead of OLED

#ifdef NO_OLED
  #define DISPLAY_PRINT(x)   Serial.print(x)
  #define DISPLAY_PRINTLN(x) Serial.println(x)
#else
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
#endif

#include <FlashStorage.h>
#include <RTClib.h>

// =============================================================================
// HARDWARE PINS
// =============================================================================
// XIAO SAMD21 pin assignments (Block 2). Encoder pins are CLK/DT/SW.
// MS1/MS2/MS3 are set by the on-board DIP switch, NOT the MCU (no defines).
// No fan on Block 2 (no FAN pin).
#define STEP_PIN        0    // D0
#define DIR_PIN         1    // D1
#define ENABLE_PIN      2    // D2
#define ENC_CLK_PIN     3    // D3 (interrupt-capable)
#define ENC_DT_PIN      6    // D6
#define ENC_SW_PIN      7    // D7
#define LIMIT_PIN       8    // D8
#define HOMING_SPEED    2000   // steps/sec during homing
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1
#define OLED_I2C_ADDR   0x3C

// =============================================================================
// DEFAULTS
// =============================================================================
#define DEFAULT_MAX_SPEED     800    // steps/sec (~2.4 in/sec)
#define DEFAULT_MIN_SPEED     50
#define DEFAULT_ACCEL_TIME    10     // tenths of seconds (10 = 1.0s ramp)
#define DEFAULT_DECEL_TIME    10     // tenths of seconds (10 = 1.0s ramp)
#define DEFAULT_DWELL_SEC     1
#define DEFAULT_DWELL_MIN_SEC 1
#define DEFAULT_DWELL_MAX_SEC 5
#define DEFAULT_SCHED_START   18   // 6 PM (24h)
#define DEFAULT_SCHED_STOP    23   // 11 PM (24h)

// Run states (cycle order: Off → Schedule → Continuous)
#define RUN_OFF        0
#define RUN_SCHEDULE   1
#define RUN_CONTINUOUS 2

// Motion modes
#define MODE_FWDBACK  0
#define MODE_WANDER   1

// =============================================================================
// JOG VELOCITY
// =============================================================================
#define JOG_MAX_LEVEL   10
#define JOG_SPEED_CEIL  8000
#define JOG_SPEED_FLOOR 30

int jogLevelToSpeed(int level) {
  // Linear: 30 at level 1, 8000 at level 10
  return JOG_SPEED_FLOOR + (JOG_SPEED_CEIL - JOG_SPEED_FLOOR) * level / JOG_MAX_LEVEL;
}

// =============================================================================
// PERSISTENT STORAGE
// =============================================================================
struct SavedData {
  uint8_t magic;
  bool    posASet;
  bool    posBSet;
  long    posA;
  long    posB;
  int     maxSpeed;
  int     accelTime;    // tenths of seconds
  int     decelTime;    // tenths of seconds
  int     dwellSec;
  int     dwellMinSec;
  int     dwellMaxSec;
  int     runMode;        // MODE_FWDBACK or MODE_WANDER
  int     runState;       // RUN_OFF, RUN_CONTINUOUS, RUN_SCHEDULE
  int     schedStart;     // start hour (0-23)
  int     schedStartMin;  // start minute (0-59)
  int     schedStop;      // stop hour (0-23)
  int     schedStopMin;   // stop minute (0-59)
};
#define EEPROM_MAGIC_VAL  0xC2  // bumped for time-based accel struct
FlashStorage(flashStore, SavedData);

// =============================================================================
// SYSTEM STATE
// =============================================================================
#ifndef NO_OLED
  Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
#endif

RTC_DS3231 rtc;
bool rtcAvailable = false;

long currentPos    = 0;
long posA          = 0;
long posB          = 0;
bool posASet       = false;
bool posBSet       = false;

volatile int  maxSpeed      = DEFAULT_MAX_SPEED;
volatile int  minSpeed      = DEFAULT_MIN_SPEED;
volatile int  accelTime     = DEFAULT_ACCEL_TIME;   // tenths of seconds — volatile for ISR access
volatile int  decelTime     = DEFAULT_DECEL_TIME;   // tenths of seconds — volatile for ISR access
volatile int  dwellSec      = DEFAULT_DWELL_SEC;
volatile int  dwellMinSec   = DEFAULT_DWELL_MIN_SEC;
volatile int  dwellMaxSec   = DEFAULT_DWELL_MAX_SEC;
int  runMode       = MODE_FWDBACK;
int  runState      = RUN_OFF;
bool pendingModeSwitch = false;  // deferred mode toggle — applied after current move

// Screensaver
#define SCREENSAVER_TIMEOUT_MS  60000  // 1 minute of inactivity
unsigned long lastActivityMs    = 0;
bool          screenSaverActive = false;
int           ssGhostX = 20, ssGhostY = 10;
int           ssGhostDx = 2, ssGhostDy = 1;
int  schedStart    = DEFAULT_SCHED_START;
int  schedStartMin = 0;
int  schedStop     = DEFAULT_SCHED_STOP;
int  schedStopMin  = 0;

// Runtime
bool ghostRunning   = false;   // true when ghost is actively traveling
int  runDirection   = 1;

// (Fan control removed for Block 2 — no fan circuit.)

// Return-to-home tracking
unsigned long lastEstopTime   = 0;
int           estopCount      = 0;
#define ESTOP_WINDOW_MS  3000  // 3 triggers within this window = go home

// Timer-interrupt motion variables (declared here, used in ISR at bottom)
enum MotionState { MOT_IDLE, MOT_ACCEL, MOT_CRUISE, MOT_DECEL, MOT_DONE };
volatile MotionState motionState = MOT_IDLE;
volatile bool     motFwd;
volatile long     motTotalSteps;
volatile long     motStepsDone;
volatile long     motAccelSteps;
volatile long     motDecelStart;
volatile uint32_t motInterval;
volatile uint32_t motMinInterval;
volatile uint32_t motMaxInterval;
volatile long     motAccelIncr;
#define TIMER_FREQ 750000UL
// Limit E-stop (ISR) timing. The ISR samples the limit every LIMIT_CHECK_MS of
// wall-clock time and requires LIMIT_ISR_CONFIRMS consecutive HIGH reads before
// firing. ANY single LOW read resets the count, so only an UNBROKEN run of HIGH
// trips it. A real press holds the switch solid for as long as the ghost rests
// against it (far longer than this window), so the E-stop still feels instant.
// Motor noise is bursty/intermittent — even a bad burst has gaps that reset the
// count — so a long unbroken-HIGH requirement rejects it. Window raised to ~40ms
// after false trips were seen roughly hourly with a 12ms window.
#define LIMIT_CHECK_MS      4    // ms between limit samples in the ISR
#define LIMIT_ISR_CONFIRMS  10   // consecutive HIGH samples required (~40ms unbroken)
volatile long motPosition = 0;
volatile bool motionComplete = false;
// DIAGNOSTIC: records which code path last ended a move, so the Diagnostics
// screen can show WHY a move stopped (helps find the premature-reversal cause).
// 0=none 1=zero-delta 2=hard-zero-clamp 3=step-complete 4=target vs count
volatile uint8_t motLastStopReason = 0;
volatile long    motLastStopSteps  = 0;   // motStepsDone at the moment it stopped
volatile long    motLastStopTotal  = 0;   // motTotalSteps for that move
// Set TRUE by the ISR limit E-stop. Distinguishes an emergency stop from a
// normal move completion (both set motionComplete). loop() checks this to fully
// shut the run down, instead of the run state machine mistaking it for "move
// done" and starting the next leg (which caused "pause while held, resume on
// release"). Latched until loop() handles it.
volatile bool    motLimitTripped   = false;
// DIAGNOSTIC noise-characterization for the limit line:
//  - motEstopHeldMs: how long D8 was continuously HIGH at the moment of a trip
//    (shown on the E-stop banner). A real press >> the threshold; a noise trip
//    will sit right at the threshold.
//  - motLimitMaxStreakMs: longest unbroken HIGH streak seen this run even when
//    it did NOT trip (shown on Diagnostics). Reveals how close noise gets to the
//    threshold — e.g. if it's regularly hitting 30ms, we're skating the edge.
volatile uint32_t motEstopHeldMs      = 0;
volatile uint32_t motLimitMaxStreakMs = 0;

// Encoder
volatile int           encDelta    = 0;
volatile unsigned long lastEncTime = 0;
int                    lastEncCLK  = HIGH;
#define ENC_DEBOUNCE_US  4000

// Jog
int jogVelocity = 0;

// Button (polled). btnWasPressed latches a confirmed press until release.
bool btnWasPressed = false;

// =============================================================================
// MENU
// =============================================================================
// Main menu: 5 items (Run, Mode, Motion, Setup, Diagnostics)
#define MAIN_MENU_COUNT 5
int mainMenuSel = 0;

const char* runStateNames[]  = { "Off", "Schedule", "Continuous" };
const char* modeNames[]      = { "Fwd-Back", "Wander" };

// Sub-menu pages
enum SettingsPage { SET_NONE, SET_MOTION, SET_SETUP, SET_LEARN_A, SET_LEARN_B, SET_SCHEDULE, SET_CLOCK, SET_DIAG };
SettingsPage settingsPage = SET_NONE;

// =============================================================================
// UNIT CONVERSION — steps/sec to in/sec for display
// Pulley: 76.2mm dia = 239.4mm/rev = 9.425 in/rev
// Output shaft: 3200 steps/rev (1600 microsteps × 2:1 gear) at 1/8 microstep
// 1 step = 9.425/3200 = 0.002945 in
// 1 in/sec = 339.4 steps/sec
// NOTE: assumes DIP = 1/8 (MS1/MS2/MS3 = H/H/L). 1/4 = 169.7, 1/16 = 678.8.
// =============================================================================
#define STEPS_PER_INCH  339.4f   // steps per inch of linear travel

// Convert steps/sec to tenths of in/sec (for display as X.X in/sec)
int stepsToTenthsInSec(int stepsPerSec) {
  return (int)(stepsPerSec * 10.0f / STEPS_PER_INCH);
}

// Convert tenths of in/sec to steps/sec (for storage)
int tenthsInSecToSteps(int tenths) {
  return (int)(tenths * STEPS_PER_INCH / 10.0f);
}

// Convert steps to feet (returns string like "12.5")
String stepsToFeet(long steps) {
  float feet = steps / (STEPS_PER_INCH * 12.0f);
  int whole = (int)feet;
  int frac = (int)((feet - whole) * 10);
  return String(whole) + "." + String(frac);
}

struct Setting { const char* label; volatile int* value; int minVal; int maxVal; int step; bool isLinear; int displayDiv; };

Setting settingsFwdBack[] = {
  { "Speed in/s",  &maxSpeed,   170, 15300, 170, true,  0 },
  { "Accel (sec)", &accelTime,  1,   50,    1,   false, 10 },
  { "Decel (sec)", &decelTime,  1,   50,    1,   false, 10 },
  { "Dwell (sec)", &dwellSec,   0,   30,    1,   false, 0 }
};
const int SETTINGS_FWDBACK_COUNT = 4;

Setting settingsWander[] = {
  { "Speed in/s",      &maxSpeed,     170, 15300, 170, true,  0 },
  { "Accel (sec)",     &accelTime,    1,   50,    1,   false, 10 },
  { "Decel (sec)",     &decelTime,    1,   50,    1,   false, 10 },
  { "Min Dwell(sec)",  &dwellMinSec,  0,   30,    1,   false, 0 },
  { "Max Dwell(sec)",  &dwellMaxSec,  1,   60,    1,   false, 0 }
};
const int SETTINGS_WANDER_COUNT = 5;

Setting* activeSettings      = settingsFwdBack;
int      activeSettingsCount = SETTINGS_FWDBACK_COUNT;

// Motion menu: Back + dynamic speed settings
#define MOTION_FIXED_COUNT 1  // just << Back
// Setup menu: Back + Schedule + Clock + Home + Learn (Learn moved to bottom to
// avoid accidental presses)
#define SETUP_FIXED_COUNT  5  // Back + Set Schedule + Set Clock + Home + Learn
int  settingsSel      = 0;
bool settingsEditing  = false;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================
void drawMainMenu();
void drawScreenSaver();
void drawMotionMenu();
void drawSetupMenu();
void drawDiagnostics();
void handleDiagnostics(int, bool);
void drawSetClock();
void drawSetSchedule();
void drawLearnScreen();
void showMessage(const char*, const char*, int);
void showSplash();
void saveFlash();
void loadFlash();
void enableMotor();
void disableMotor();
void setupMotionTimer();
void startMove(long);
void stopMotion();
bool isMotionRunning();
void doRunStep();
bool checkButton();
bool readLimitDebounced();
int  consumeEncoderTicks();
void encoderISR();
long minTravelDistance();
void updateActiveSettings();
String getTimeString12h();
String getStatusString();
bool isWithinSchedule();
bool shouldBeRunning();
void handleMainMenu(int, bool);
void handleMotionMenu(int, bool);
void handleSetupMenu(int, bool);
void handleLearnMode(int, bool);
void savePosition();
unsigned long getDwellMs();
void homeMotor();


// =============================================================================
// MOTOR ENABLE/DISABLE + HELPERS
// =============================================================================
void enableMotor()  { digitalWrite(ENABLE_PIN, LOW);  }
void disableMotor() { digitalWrite(ENABLE_PIN, HIGH); }

long minTravelDistance() {
  // Minimum distance = accel ramp + decel ramp (based on time settings)
  float avgSpeed = ((float)maxSpeed + (float)minSpeed) / 2.0f;
  long accelDist = (long)(avgSpeed * accelTime / 10.0f);
  long decelDist = (long)(avgSpeed * decelTime / 10.0f);
  return accelDist + decelDist;
}

// =============================================================================
// HOMING — slow jog toward limit switch until triggered
// =============================================================================
void homeMotor() {
#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0, 20); display.println("  Homing...");
  display.setCursor(0, 36); display.println("  Finding home pos");
  display.display();
#else
  Serial.println("Homing...");
#endif

  enableMotor();

  // Jog in reverse (toward home switch) at HOMING_SPEED
  digitalWrite(DIR_PIN, LOW);  // reverse direction
  delayMicroseconds(5);

  unsigned long stepPeriodUs = 1000000UL / (unsigned long)HOMING_SPEED;

  // Step until limit switch triggers (with debounce — 3 consecutive HIGH reads)
  while (true) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(2);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(stepPeriodUs > 2 ? stepPeriodUs - 2 : 1);

    // Check limit switch with debounce
    if (digitalRead(LIMIT_PIN) == HIGH) {
      delay(5);
      if (digitalRead(LIMIT_PIN) == HIGH) {
        delay(5);
        if (digitalRead(LIMIT_PIN) == HIGH) {
          break;  // confirmed — switch is really triggered
        }
      }
    }
  }

  // Switch triggered — stop and set position to zero
  currentPos = 0;
  motPosition = 0;

  // Back off the switch slightly (100 steps forward), then set this as home (zero)
  digitalWrite(DIR_PIN, HIGH);
  delayMicroseconds(5);
  for (int i = 0; i < 100; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(2);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(stepPeriodUs > 2 ? stepPeriodUs - 2 : 1);
  }
  // THIS is home — position zero. Cannot go backward from here.
  currentPos = 0;
  motPosition = 0;

  disableMotor();

#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0, 28); display.println("  Home found!");
  display.display();
  delay(800);
#else
  Serial.println("Home found!");
#endif
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.begin(9600);
#ifdef NO_OLED
  unsigned long sw = millis();
  while (!Serial && (millis() - sw < 3000)) {}
  Serial.println("--- v1.3 NO_OLED mode ---");
#endif

  pinMode(STEP_PIN, OUTPUT); pinMode(DIR_PIN, OUTPUT); pinMode(ENABLE_PIN, OUTPUT);
  // MS1/MS2/MS3 are set by the DIP switch on the board — not driven here.
  digitalWrite(ENABLE_PIN, HIGH);

  pinMode(LIMIT_PIN, INPUT_PULLUP);

  pinMode(ENC_CLK_PIN, INPUT_PULLUP);
  pinMode(ENC_DT_PIN,  INPUT_PULLUP);
  pinMode(ENC_SW_PIN,  INPUT_PULLUP);
  lastEncCLK = digitalRead(ENC_CLK_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), encoderISR, CHANGE);

#ifndef NO_OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    pinMode(LED_BUILTIN, OUTPUT);
    while (true) { digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); delay(200); }
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
#endif

  // Seed RNG (for Wander mode) from a floating spare analog pin.
  // A9/A10 (D9/D10, AUX1/AUX2) are unused GPIO — reading one gives noise for a seed.
  // WARNING: analogRead() reconfigures a pin to analog input; only use a spare pin.
  randomSeed(analogRead(A10));  // AUX2 (D10) — unused, safe to read

  // Initialize DS3231 RTC
  if (rtc.begin()) {
    rtcAvailable = true;
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  loadFlash();
  updateActiveSettings();
  setupMotionTimer();
  homeMotor();

  // Auto-learn if no positions are set
  if (!posASet || !posBSet) {
    showMessage("No positions", "Starting learn", 1500);
    settingsPage = SET_LEARN_A;
    jogVelocity = 0;
    enableMotor();
    drawLearnScreen();
  } else {
    showSplash();
    delay(1500);
    settingsPage = SET_NONE;
    lastActivityMs = millis();
    drawMainMenu();
  }
}

// =============================================================================
// MAIN LOOP
// =============================================================================
unsigned long dwellEndMs  = 0;
bool          dwelling    = false;
bool          needsMove   = false;  // flag: next move target ready

void loop() {
  int  ticks    = consumeEncoderTicks();
  bool btnShort = checkButton();

  // --- Screensaver: button is DEAF while it's showing ---
  // During a long unattended run the screensaver is up ~all the time. A phantom
  // button press (noise on D7 that slips past the debounce over a long run) would
  // otherwise reach the menu and could toggle Run->Off wherever the cursor sat.
  // So while the screensaver is active we SWALLOW the button entirely — it does
  // nothing and cannot wake. Only encoder ROTATION wakes the screen; noise can't
  // fake coherent rotation ticks. On wake we clear any latched button state so a
  // phantom press that happened during the saver never fires afterward.
  if (screenSaverActive) {
    btnShort = false;                 // swallow the button while asleep
    if (ticks != 0) {
      // Rotation = real human intent -> wake up. Consume the tick so the waking
      // turn only wakes (doesn't also scroll the menu underneath).
      screenSaverActive = false;
      ticks = 0;
      btnWasPressed = false;          // discard any press latched during the saver
      lastActivityMs = millis();
      if (settingsPage == SET_MOTION) drawMotionMenu();
      else if (settingsPage == SET_SETUP) drawSetupMenu();
      else drawMainMenu();
      return;                         // done this iteration; menu is live next loop
    } else {
      // Still asleep — animate and skip all menu/button handling, but let the
      // motor/schedule logic below keep running.
      static unsigned long lastSSFrame = 0;
      if (millis() - lastSSFrame > 200) {
        drawScreenSaver();
        lastSSFrame = millis();
      }
    }
  } else {
    // Awake: any human input (rotation or a real button press) resets the
    // inactivity timer so the saver only kicks in after genuine idle time.
    if (ticks != 0 || btnShort) {
      lastActivityMs = millis();
    }
    // Activate screensaver after timeout (main menu / run only, not editing)
    if (settingsPage == SET_NONE && !settingsEditing &&
        millis() - lastActivityMs > SCREENSAVER_TIMEOUT_MS) {
      screenSaverActive = true;
      btnWasPressed = false;          // enter the saver with no latched press
    }
  }

  // --- Button stops ghost immediately from any screen ---
  if (btnShort && ghostRunning && settingsPage != SET_MOTION && settingsPage != SET_SETUP && settingsPage != SET_LEARN_A && settingsPage != SET_LEARN_B) {
    // If on main menu and ghost is running, button on "Run" will cycle state
    // which handles stopping. But if user isn't on Run line, stop anyway.
  }

  // --- Learn mode ---
  if (settingsPage == SET_LEARN_A || settingsPage == SET_LEARN_B) {
    handleLearnMode(ticks, btnShort);
    return;
  }

  // --- Menu handling (always responsive) ---
  if (settingsPage == SET_MOTION) {
    handleMotionMenu(ticks, btnShort);
  } else if (settingsPage == SET_SETUP) {
    handleSetupMenu(ticks, btnShort);
  } else if (settingsPage == SET_DIAG) {
    handleDiagnostics(ticks, btnShort);
    // Live refresh (~5 Hz) so position/velocity update while watching, without
    // bogging the I2C bus. Still falls through to the run state machine below so
    // the ghost keeps running and you can watch real velocity during a move.
    static unsigned long lastDiagDrawMs = 0;
    if (settingsPage == SET_DIAG && millis() - lastDiagDrawMs > 200) {
      drawDiagnostics();
      lastDiagDrawMs = millis();
    }
  } else {
    handleMainMenu(ticks, btnShort);
  }

  // --- Limit E-stop raised by the ISR during a run ---
  // The ISR already halted the motor and flagged this as an EMERGENCY stop (not
  // a normal move completion). Fully shut the run down here, BEFORE the run state
  // machine below can mistake motionComplete for "move done" and start the next
  // leg. This is what makes a limit trip actually end the run even if the switch
  // is released quickly (previously it just paused and resumed on release).
  if (motLimitTripped) {
    motLimitTripped = false;
    noInterrupts();
    currentPos = motPosition;
    motionComplete = false;
    interrupts();
    stopMotion();
    disableMotor();
    ghostRunning = false;
    dwelling     = false;
    runState     = RUN_OFF;
    saveFlash();

    // Triple-trigger within the window still sends the ghost home
    unsigned long now = millis();
    estopCount = (now - lastEstopTime < ESTOP_WINDOW_MS) ? estopCount + 1 : 1;
    lastEstopTime = now;
    if (estopCount >= 3) {
      estopCount = 0;
      showMessage("Going home...", "", 500);
      enableMotor();
      startMove(0);
      while (!motionComplete) { /* wait */ }
      noInterrupts();
      currentPos = motPosition;
      motionComplete = false;
      interrupts();
      disableMotor();
      showMessage("Home!", "", 1000);
    } else {
      char l2[20];
      snprintf(l2, sizeof(l2), "HIGH held %lums", (unsigned long)motEstopHeldMs);
      showMessage("E-STOP!", l2, 3000);   // DIAG: show measured HIGH duration
    }
    lastActivityMs = millis();
    screenSaverActive = false;
    drawMainMenu();
    return;   // done this iteration
  }

  // --- Ghost run state machine (non-blocking) ---
  if (shouldBeRunning() && posASet && posBSet) {
    if (!ghostRunning) {
      // Starting fresh.
      // SCHEDULE runs re-home against the limit switch first. While idle the
      // motor is de-energized, so the ghost can drift in the wind — re-homing
      // guarantees a true position reference so the first move doesn't crash
      // into the pulley end. homeMotor() handles its own enable/disable.
      if (runState == RUN_SCHEDULE) homeMotor();
      enableMotor();   // homeMotor() leaves the motor disabled
      ghostRunning = true;
      runDirection = 1;
      needsMove = false;
      dwelling = false;
      // Move to start position (posA)
      startMove(posA);
      if (settingsPage == SET_NONE) drawMainMenu();
    }

    // Check if current move finished
    if (motionComplete && !dwelling && ghostRunning) {
      noInterrupts();
      currentPos = motPosition;
      motionComplete = false;
      interrupts();
      
      // Apply deferred mode switch if pending
      if (pendingModeSwitch) {
        pendingModeSwitch = false;
        runMode = (runMode == MODE_FWDBACK) ? MODE_WANDER : MODE_FWDBACK;
        updateActiveSettings();
        saveFlash();
      }
      
      // Start dwell
      dwelling = true;
      dwellEndMs = millis() + getDwellMs();
      if (settingsPage == SET_NONE) drawMainMenu();
    }

    // Check if dwell is over — start next move
    if (dwelling && millis() >= dwellEndMs) {
      dwelling = false;
      // Calculate next target
      long target;
      if (runMode == MODE_FWDBACK) {
        target = (runDirection == 1) ? posB : posA;
        runDirection = -runDirection;
      } else {
        // Wander
        long lo = min(posA, posB);
        long hi = max(posA, posB);
        long minDist = minTravelDistance();
        target = currentPos;  // fallback
        for (int attempt = 0; attempt < 20; attempt++) {
          target = random(lo, hi + 1);
          if (abs(target - currentPos) >= minDist) break;
          if (attempt == 19) {
            target = (abs(hi - currentPos) > abs(lo - currentPos)) ? hi : lo;
          }
        }
      }
      startMove(target);
    }
  } else {
    // Should not be running
    if (ghostRunning) {
      stopMotion();
      ghostRunning = false;
      dwelling = false;
      // SCHEDULE just ended: re-home against the limit switch (not just a move to
      // coordinate 0). A true re-home corrects any drift accumulated during the
      // run and parks the ghost at a verified home, so overnight wind drift
      // starts from a known point. homeMotor() handles its own enable/disable.
      if (runState == RUN_SCHEDULE) {
        homeMotor();
      }
      disableMotor();
      if (settingsPage == SET_NONE) drawMainMenu();
    }
  }

  // Periodic clock refresh on main menu (every 30 seconds) — skip if screensaver active
  static unsigned long lastClockRefreshMs = 0;
  if (!screenSaverActive && settingsPage == SET_NONE && millis() - lastClockRefreshMs > 30000) {
    drawMainMenu();
    lastClockRefreshMs = millis();
  }

  // NOTE: the limit E-stop DURING A RUN is handled entirely by the ISR check +
  // the motLimitTripped handler near the top of loop() — both non-blocking. The
  // old blocking loop() backstop here (a ~45ms delay() spin on every HIGH read)
  // was removed: when D8 picked up motor noise it repeatedly entered that spin
  // and starved checkButton(), making the button unreliable during a run. The
  // ISR owns the running case; the block below still covers the NOT-running case.

  // MANUAL HOME: limit switch triggered while ghost is NOT running
  // 3 triggers within 3 seconds sends ghost home
  if (!ghostRunning && digitalRead(LIMIT_PIN) == HIGH) {
    delay(5);
    if (digitalRead(LIMIT_PIN) == HIGH) {
      delay(5);
      if (digitalRead(LIMIT_PIN) == HIGH) {
        unsigned long now = millis();
        if (now - lastEstopTime < ESTOP_WINDOW_MS) {
          estopCount++;
        } else {
          estopCount = 1;
        }
        lastEstopTime = now;

        if (estopCount >= 3) {
          estopCount = 0;
          showMessage("Going home...", "", 500);
          enableMotor();
          startMove(0);
          while (!motionComplete) { /* wait */ }
          noInterrupts();
          currentPos = motPosition;
          motionComplete = false;
          interrupts();
          disableMotor();
          showMessage("Home!", "", 1000);
          drawMainMenu();
        }
        // Wait for switch to release before continuing
        while (digitalRead(LIMIT_PIN) == HIGH) { delay(10); }
        delay(100);  // extra debounce after release
      }
    }
  }
}

// =============================================================================
// SHOULD THE GHOST BE RUNNING?
// =============================================================================
bool shouldBeRunning() {
  if (runState == RUN_OFF) return false;
  if (runState == RUN_CONTINUOUS) return true;
  if (runState == RUN_SCHEDULE) return isWithinSchedule();
  return false;
}

bool isWithinSchedule() {
  if (!rtcAvailable) return false;
  DateTime now = rtc.now();
  int nowMins   = now.hour() * 60 + now.minute();
  int startMins = schedStart * 60 + schedStartMin;
  int stopMins  = schedStop * 60 + schedStopMin;
  if (startMins <= stopMins) {
    return (nowMins >= startMins && nowMins < stopMins);
  } else {
    // Wraps midnight (e.g., start=10:30PM, stop=6:00AM)
    return (nowMins >= startMins || nowMins < stopMins);
  }
}

// =============================================================================
// MAIN MENU HANDLER
// =============================================================================
void handleMainMenu(int ticks, bool btnShort) {
  if (ticks != 0) {
    mainMenuSel = constrain(mainMenuSel + (ticks > 0 ? 1 : -1), 0, MAIN_MENU_COUNT - 1);
    drawMainMenu();
  }
  if (btnShort) {
    switch (mainMenuSel) {
      case 0: // Run — cycle state
        runState = (runState + 1) % 3;
        if (runState == RUN_CONTINUOUS && (!posASet || !posBSet)) {
          showMessage("Set positions", "first!", 1500);
          runState = RUN_OFF;
        }
        if (runState == RUN_SCHEDULE && (!posASet || !posBSet)) {
          showMessage("Set positions", "first!", 1500);
          runState = RUN_OFF;
        }
        if (runState == RUN_OFF && ghostRunning) {
          stopMotion();
          disableMotor();
          ghostRunning = false;
          dwelling = false;
        }
        saveFlash();
        drawMainMenu();
        break;
      case 1: // Mode — toggle (deferred if mid-move)
        if (isMotionRunning()) {
          pendingModeSwitch = true;
          showMessage("Mode switch", "after move", 800);
          drawMainMenu();
        } else {
          runMode = (runMode == MODE_FWDBACK) ? MODE_WANDER : MODE_FWDBACK;
          updateActiveSettings();
          saveFlash();
          drawMainMenu();
        }
        break;
      case 2: // Motion — enter motion sub-menu
        settingsPage = SET_MOTION;
        settingsSel = 0;
        settingsEditing = false;
        drawMotionMenu();
        break;
      case 3: // Setup — enter setup sub-menu
        settingsPage = SET_SETUP;
        settingsSel = 0;
        settingsEditing = false;
        drawSetupMenu();
        break;
      case 4: // Diagnostics — live position/velocity readout
        settingsPage = SET_DIAG;
        settingsEditing = false;
        drawDiagnostics();
        break;
    }
  }
}

// =============================================================================
// MOTION MENU HANDLER
// =============================================================================
void handleMotionMenu(int ticks, bool btnShort) {
  int totalItems = MOTION_FIXED_COUNT + activeSettingsCount;

  if (!settingsEditing) {
    if (ticks != 0) {
      settingsSel = constrain(settingsSel + (ticks > 0 ? 1 : -1), 0, totalItems - 1);
      drawMotionMenu();
    }
    if (btnShort) {
      if (settingsSel == 0) {
        // << Back
        settingsPage = SET_NONE;
        drawMainMenu();
      } else {
        settingsEditing = true;
        drawMotionMenu();
      }
    }
  } else {
    int idx = settingsSel - MOTION_FIXED_COUNT;
    if (ticks != 0) {
      int delta = (ticks > 0 ? 1 : -1) * activeSettings[idx].step;
      *activeSettings[idx].value = constrain(
        *activeSettings[idx].value + delta,
        activeSettings[idx].minVal, activeSettings[idx].maxVal);
      drawMotionMenu();
    }
    if (btnShort) {
      settingsEditing = false;
      saveFlash();
      drawMotionMenu();
    }
  }
}

// =============================================================================
// SETUP MENU HANDLER
// =============================================================================
void handleSetupMenu(int ticks, bool btnShort) {
  if (ticks != 0) {
    settingsSel = constrain(settingsSel + (ticks > 0 ? 1 : -1), 0, SETUP_FIXED_COUNT - 1);
    drawSetupMenu();
  }
  if (btnShort) {
    switch (settingsSel) {
      case 0: // << Back
        settingsPage = SET_NONE;
        drawMainMenu();
        break;
      case 1: // Set Schedule
        drawSetSchedule();
        break;
      case 2: // Set Clock
        drawSetClock();
        break;
      case 3: // Home — manual re-home against the limit switch
        // Only when not actively running (homing mid-run would fight the run
        // state machine). homeMotor() handles its own enable/disable and shows
        // its own "Homing..." screen.
        if (!ghostRunning) {
          homeMotor();
          drawSetupMenu();
        }
        break;
      case 4: // Learn Positions (moved to bottom to avoid accidental presses)
        settingsPage = SET_LEARN_A;
        jogVelocity = 0;
        enableMotor();
        drawLearnScreen();
        break;
    }
  }
}


// =============================================================================
// LEARN MODE
// =============================================================================
#define JOG_SPEED  3000   // fixed jog speed in steps/sec for learn mode

void handleLearnMode(int ticks, bool btnShort) {
  // Encoder sets direction: CW = forward, CCW = reverse, no movement = stop
  if (ticks != 0) {
    if (ticks > 0) {
      jogVelocity = 1;   // forward
    } else {
      // Only allow reverse if not at home
      if (currentPos > 0) {
        jogVelocity = -1;  // reverse
      } else {
        jogVelocity = 0;
      }
    }
    drawLearnScreen();
  }

  // Motor control
  if (jogVelocity != 0) {
    bool fwd = (jogVelocity > 0);
    
    // Block reverse at zero — check BEFORE starting motor
    if (!fwd) {
      noInterrupts();
      long pos = isMotionRunning() ? motPosition : currentPos;
      interrupts();
      if (pos <= 0) {
        jogVelocity = 0;
        if (isMotionRunning()) {
          stopMotion();
        }
        currentPos = 0;
        noInterrupts();
        motPosition = 0;
        interrupts();
        disableMotor();
        drawLearnScreen();
        goto learnEnd;
      }
    }
    
    enableMotor();
    
    if (!isMotionRunning()) {
      // Start stepping
      digitalWrite(DIR_PIN, fwd ? HIGH : LOW);
      delayMicroseconds(5);
      motFwd = fwd;
      motTotalSteps = 1000000;
      motStepsDone = 0;
      motPosition = currentPos;
      motionComplete = false;
      motDecelStart = 999999;
      motAccelSteps = 0;
      motMinInterval = TIMER_FREQ / (uint32_t)JOG_SPEED;
      motMaxInterval = motMinInterval;
      motInterval = motMinInterval;
      motionState = MOT_CRUISE;
      
      TC4->COUNT16.CC[0].reg = (uint16_t)min(motInterval, 65535UL);
      while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
      TC4->COUNT16.COUNT.reg = 0;
      while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
      TC4->COUNT16.CTRLA.bit.ENABLE = 1;
      while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
    } else if (motFwd != fwd) {
      // Direction changed — stop first, will restart next loop
      stopMotion();
      noInterrupts();
      currentPos = motPosition;
      interrupts();
    }
  } else {
    // Stopped
    if (isMotionRunning()) {
      stopMotion();
      noInterrupts();
      currentPos = motPosition;
      interrupts();
    }
  }

  // Hard zero clamp — check every loop while running reverse
  if (isMotionRunning() && !motFwd) {
    noInterrupts();
    long pos = motPosition;
    interrupts();
    if (pos <= 0) {
      stopMotion();
      currentPos = 0;
      noInterrupts();
      motPosition = 0;
      interrupts();
      jogVelocity = 0;
      disableMotor();
      drawLearnScreen();
      return;
    }
  }
  
  // Sync position while running forward
  if (isMotionRunning()) {
    noInterrupts();
    currentPos = motPosition;
    interrupts();
    // Periodic screen update while moving (every 500ms to minimize I2C interference)
    static unsigned long lastLearnDrawMs = 0;
    if (millis() - lastLearnDrawMs > 500) {
      drawLearnScreen();
      lastLearnDrawMs = millis();
    }
  }
  
  // Catch ISR zero-stop (motor stopped itself at position 0)
  if (!isMotionRunning() && jogVelocity < 0) {
    noInterrupts();
    long pos = motPosition;
    interrupts();
    if (pos <= 0) {
      jogVelocity = 0;
      currentPos = 0;
      noInterrupts();
      motPosition = 0;
      interrupts();
      drawLearnScreen();
    }
  }

  // Button = save position
  if (btnShort) {
    if (isMotionRunning()) {
      stopMotion();
      noInterrupts();
      currentPos = motPosition;
      interrupts();
    }
    jogVelocity = 0;
    disableMotor();
    savePosition();
  }

learnEnd:;
}

void savePosition() {
  if (settingsPage == SET_LEARN_A) {
    posA = currentPos; posASet = true; saveFlash();
    settingsPage = SET_LEARN_B;
    drawLearnScreen();
  } else {
    posB = currentPos; posBSet = true; saveFlash();
    showMessage("Positions saved!", "", 1000);
    settingsPage = SET_NONE;
    settingsSel = 0;
    lastActivityMs = millis();
    drawMainMenu();
  }
}

// =============================================================================
// DWELL TIME HELPER
// =============================================================================
unsigned long getDwellMs() {
  if (runMode == MODE_FWDBACK) {
    return (unsigned long)dwellSec * 1000UL;
  } else {
    int dSec = random(dwellMinSec, dwellMaxSec + 1);
    return (unsigned long)dSec * 1000UL;
  }
}

// =============================================================================
// SET SCHEDULE — pick start and stop times (hour + minute for each)
// =============================================================================
void drawSetSchedule() {
  if (!rtcAvailable) {
    showMessage("RTC not found!", "", 1500);
    drawSetupMenu();
    return;
  }

  int startH = schedStart;
  int startM = schedStartMin;
  int stopH  = schedStop;
  int stopM  = schedStopMin;
  int step = 0;  // 0=start hour, 1=start min, 2=stop hour, 3=stop min

  while (true) {
    // Convert to 12h for display
    int startH12 = startH % 12; if (startH12 == 0) startH12 = 12;
    bool startPM = (startH >= 12);
    int stopH12 = stopH % 12; if (stopH12 == 0) stopH12 = 12;
    bool stopPM = (stopH >= 12);

#ifndef NO_OLED
    display.clearDisplay(); display.setTextSize(1);
    display.setCursor(0, 0); display.println("== SET SCHEDULE ==");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
    display.setCursor(0, 16);
    display.print("Start: "); display.print(startH12);
    display.print(":"); if (startM < 10) display.print("0");
    display.print(startM); display.print(" ");
    display.println(startPM ? "PM" : "AM");
    display.setCursor(0, 30);
    display.print("Stop:  "); display.print(stopH12);
    display.print(":"); if (stopM < 10) display.print("0");
    display.print(stopM); display.print(" ");
    display.println(stopPM ? "PM" : "AM");
    display.setCursor(0, 46);
    if (step == 0) display.print("> Start hr  ");
    else if (step == 1) display.print("> Start min ");
    else if (step == 2) display.print("> Stop hr   ");
    else display.print("> Stop min  ");
    display.println("Btn=next");
    display.display();
#else
    Serial.print("Schedule: Start="); Serial.print(startH12);
    Serial.print(":"); if (startM < 10) Serial.print("0");
    Serial.print(startM); Serial.print(startPM ? "PM" : "AM");
    Serial.print(" Stop="); Serial.print(stopH12);
    Serial.print(":"); if (stopM < 10) Serial.print("0");
    Serial.print(stopM); Serial.println(stopPM ? "PM" : "AM");
#endif

    while (true) {
      int t = consumeEncoderTicks();
      if (t != 0) {
        if (step == 0) {
          startH = (startH + (t > 0 ? 1 : -1) + 24) % 24;
        } else if (step == 1) {
          startM = (startM + (t > 0 ? 5 : -5) + 60) % 60;  // 5-min increments
        } else if (step == 2) {
          stopH = (stopH + (t > 0 ? 1 : -1) + 24) % 24;
        } else {
          stopM = (stopM + (t > 0 ? 5 : -5) + 60) % 60;  // 5-min increments
        }
        break;
      }
      if (checkButton()) {
        step++;
        if (step > 3) {
          schedStart    = startH;
          schedStartMin = startM;
          schedStop     = stopH;
          schedStopMin  = stopM;
          saveFlash();
          showMessage("Schedule set!", "", 1000);
          settingsPage = SET_SETUP;
          drawSetupMenu();
          return;
        }
        break;
      }
    }
  }
}

// =============================================================================
// SET CLOCK — 12-hour picker (hour, minute, AM/PM)
// =============================================================================
void drawSetClock() {
  if (!rtcAvailable) {
    showMessage("RTC not found!", "", 1500);
    drawSetupMenu();
    return;
  }

  DateTime now = rtc.now();
  int hour24 = now.hour();
  int minute = now.minute();
  bool isPM = (hour24 >= 12);
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  int step = 0;

  while (true) {
#ifndef NO_OLED
    display.clearDisplay(); display.setTextSize(1);
    display.setCursor(0, 0); display.println("== SET CLOCK ==");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
    display.setCursor(20, 22); display.setTextSize(2);
    if (hour12 < 10) display.print(" ");
    display.print(hour12); display.print(":");
    if (minute < 10) display.print("0");
    display.print(minute); display.print(" ");
    display.print(isPM ? "PM" : "AM");
    display.setTextSize(1);
    display.setCursor(0, 46);
    if (step == 0) display.println("Turn=hour  Btn=next");
    else if (step == 1) display.println("Turn=min   Btn=next");
    else display.println("Turn=AM/PM Btn=save");
    int ulY = 38;
    if (step == 0) display.drawLine(20, ulY, 44, ulY, SSD1306_WHITE);
    else if (step == 1) display.drawLine(50, ulY, 74, ulY, SSD1306_WHITE);
    else display.drawLine(80, ulY, 104, ulY, SSD1306_WHITE);
    display.display();
#else
    Serial.print("SET CLOCK: "); Serial.print(hour12); Serial.print(":");
    if (minute < 10) Serial.print("0");
    Serial.print(minute); Serial.print(" "); Serial.println(isPM ? "PM" : "AM");
#endif

    while (true) {
      int t = consumeEncoderTicks();
      if (t != 0) {
        if (step == 0) {
          hour12 += (t > 0 ? 1 : -1);
          if (hour12 > 12) hour12 = 1;
          if (hour12 < 1) hour12 = 12;
        } else if (step == 1) {
          minute += (t > 0 ? 1 : -1);
          if (minute > 59) minute = 0;
          if (minute < 0) minute = 59;
        } else {
          isPM = !isPM;
        }
        break;
      }
      if (checkButton()) {
        step++;
        if (step > 2) {
          int newHour24 = hour12 % 12;
          if (isPM) newHour24 += 12;
          rtc.adjust(DateTime(2024, 1, 1, newHour24, minute, 0));
          showMessage("Clock set!", "", 1000);
          settingsPage = SET_SETUP;
          drawSetupMenu();
          return;
        }
        break;
      }
    }
  }
}


// =============================================================================
// DISPLAY FUNCTIONS
// =============================================================================

String getTimeString12h() {
  if (!rtcAvailable) return "No RTC";
  DateTime now = rtc.now();
  int hour24 = now.hour();
  int minute = now.minute();
  bool isPM = (hour24 >= 12);
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  String s = String(hour12) + ":";
  if (minute < 10) s += "0";
  s += String(minute) + (isPM ? "PM" : "AM");
  return s;
}

String getStatusString() {
  if (runState == RUN_OFF) {
    return "Off  " + getTimeString12h();
  }
  if (runState == RUN_CONTINUOUS) {
    return ghostRunning ? "Running  " + getTimeString12h() : "Starting " + getTimeString12h();
  }
  if (runState == RUN_SCHEDULE) {
    if (isWithinSchedule()) {
      return "Running  " + getTimeString12h();
    } else {
      // Show next start time with minutes
      int h12 = schedStart % 12; if (h12 == 0) h12 = 12;
      bool pm = (schedStart >= 12);
      String t = "Next:" + String(h12) + ":";
      if (schedStartMin < 10) t += "0";
      t += String(schedStartMin) + (pm ? "PM " : "AM ") + getTimeString12h();
      return t;
    }
  }
  return getTimeString12h();
}

// =============================================================================
// SCREENSAVER — bouncing ghost sprite with time display
// =============================================================================
#define SS_GHOST_W  16
#define SS_GHOST_H  20
// Simple ghost bitmap (16x20 pixels)
static const uint8_t ssGhostBmp[] PROGMEM = {
  0b00000111, 0b11100000,
  0b00011111, 0b11111000,
  0b00111111, 0b11111100,
  0b01111111, 0b11111110,
  0b01111111, 0b11111110,
  0b11111111, 0b11111111,
  0b11100111, 0b11100111,
  0b11000011, 0b11000011,
  0b11100111, 0b11100111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111001, 0b10011111,
  0b11110000, 0b00001111,
  0b11111001, 0b10011111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11011101, 0b11011101,
  0b10001000, 0b10001000,
  0b00000000, 0b00000000,
};

void drawScreenSaver() {
#ifndef NO_OLED
  // Move ghost
  ssGhostX += ssGhostDx;
  ssGhostY += ssGhostDy;
  // Bounce off walls (leave room for time at bottom)
  if (ssGhostX <= 0 || ssGhostX >= OLED_WIDTH - SS_GHOST_W) ssGhostDx = -ssGhostDx;
  if (ssGhostY <= 0 || ssGhostY >= 50 - SS_GHOST_H) ssGhostDy = -ssGhostDy;
  ssGhostX = constrain(ssGhostX, 0, OLED_WIDTH - SS_GHOST_W);
  ssGhostY = constrain(ssGhostY, 0, 50 - SS_GHOST_H);

  display.clearDisplay();
  display.drawBitmap(ssGhostX, ssGhostY, ssGhostBmp, SS_GHOST_W, SS_GHOST_H, SSD1306_WHITE);
  // Bottom info: labeled time + schedule status
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print("Now: "); display.print(getTimeString12h());
  if (runState == RUN_SCHEDULE) {
    display.setCursor(0, 57);
    if (ghostRunning) {
      display.print("Status: Running");
    } else {
      int h12 = schedStart % 12; if (h12 == 0) h12 = 12;
      bool pm = (schedStart >= 12);
      display.print("Next: "); display.print(h12); display.print(":");
      if (schedStartMin < 10) display.print("0");
      display.print(schedStartMin); display.print(pm ? "PM" : "AM");
    }
  } else if (ghostRunning) {
    display.setCursor(0, 57);
    display.print("Status: Running");
  }
  display.display();
#endif
}

void drawMainMenu() {
#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  // Title
  display.setCursor(0, 0); display.println("GHOST PULLEY");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
  // Line 1: Run state
  display.setCursor(0, 12);
  display.print(mainMenuSel == 0 ? "> " : "  ");
  display.print("Run: "); display.println(runStateNames[runState]);
  // Line 2: Mode
  display.setCursor(0, 20);
  display.print(mainMenuSel == 1 ? "> " : "  ");
  display.print("Mode: "); display.println(modeNames[runMode]);
  // Line 3: Motion
  display.setCursor(0, 28);
  display.print(mainMenuSel == 2 ? "> " : "  ");
  display.println("Motion");
  // Line 4: Setup
  display.setCursor(0, 36);
  display.print(mainMenuSel == 3 ? "> " : "  ");
  display.println("Setup");
  // Line 5: Diagnostics
  display.setCursor(0, 44);
  display.print(mainMenuSel == 4 ? "> " : "  ");
  display.println("Diagnostics");
  // Status bar
  display.drawLine(0, 53, 127, 53, SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print(getStatusString());
  display.display();
#else
  Serial.println("==== GHOST PULLEY ====");
  Serial.print(mainMenuSel == 0 ? "> " : "  ");
  Serial.print("Run: "); Serial.println(runStateNames[runState]);
  Serial.print(mainMenuSel == 1 ? "> " : "  ");
  Serial.print("Mode: "); Serial.println(modeNames[runMode]);
  Serial.print(mainMenuSel == 2 ? "> " : "  ");
  Serial.println("Motion");
  Serial.print(mainMenuSel == 3 ? "> " : "  ");
  Serial.println("Setup");
  Serial.print(mainMenuSel == 4 ? "> " : "  ");
  Serial.println("Diagnostics");
  Serial.println("----------------------");
  Serial.println(getStatusString());
#endif
}

void drawMotionMenu() {
  int totalItems = MOTION_FIXED_COUNT + activeSettingsCount;
#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0, 0); display.println("== MOTION ==");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  int start = max(0, min(settingsSel - 1, totalItems - 3));
  for (int i = 0; i < 3; i++) {
    int idx = start + i;
    if (idx >= totalItems) break;
    display.setCursor(0, 14 + i * 13);
    bool sel = (idx == settingsSel);
    if (idx == 0) { display.print(sel ? "> " : "  "); display.print("<< Back"); }
    else {
      int si = idx - MOTION_FIXED_COUNT;
      display.print((sel && settingsEditing) ? "* " : (sel ? "> " : "  "));
      display.print(activeSettings[si].label);
      display.print(":");
      if (activeSettings[si].isLinear) {
        // Display as X.X in/sec
        int tenths = stepsToTenthsInSec(*activeSettings[si].value);
        display.print(tenths / 10); display.print("."); display.print(tenths % 10);
      } else if (activeSettings[si].displayDiv > 1) {
        // Display as X.X (value stored in tenths)
        int val = *activeSettings[si].value;
        display.print(val / activeSettings[si].displayDiv);
        display.print(".");
        display.print(val % activeSettings[si].displayDiv);
      } else {
        display.print(*activeSettings[si].value);
      }
    }
  }
  display.setCursor(0, 55);
  display.print(getTimeString12h());
  display.display();
#else
  Serial.println("--- MOTION ---");
  Serial.println(settingsSel == 0 ? "> << Back" : "  << Back");
  for (int i = 0; i < activeSettingsCount; i++) {
    int idx = i + MOTION_FIXED_COUNT;
    bool sel = (idx == settingsSel);
    Serial.print((sel && settingsEditing) ? "* " : (sel ? "> " : "  "));
    Serial.print(activeSettings[i].label); Serial.print(": ");
    if (activeSettings[i].isLinear) {
      int tenths = stepsToTenthsInSec(*activeSettings[i].value);
      Serial.print(tenths / 10); Serial.print("."); Serial.println(tenths % 10);
    } else if (activeSettings[i].displayDiv > 1) {
      int val = *activeSettings[i].value;
      Serial.print(val / activeSettings[i].displayDiv);
      Serial.print(".");
      Serial.println(val % activeSettings[i].displayDiv);
    } else {
      Serial.println(*activeSettings[i].value);
    }
  }
#endif
}

// Setup menu item labels (order matches the switch in handleSetupMenu).
// Learn Positions is intentionally last so it isn't pressed by accident.
static const char* const setupItems[SETUP_FIXED_COUNT] = {
  "<< Back", "Set Schedule", "Set Clock", "Home", "Learn Positions"
};

void drawSetupMenu() {
#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0, 0); display.println("== SETUP ==");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  // Scrolling 4-line window (5 items won't all fit on the 64px screen).
  int start = max(0, min(settingsSel - 1, SETUP_FIXED_COUNT - 4));
  for (int i = 0; i < 4; i++) {
    int idx = start + i;
    if (idx >= SETUP_FIXED_COUNT) break;
    display.setCursor(0, 16 + i * 12);
    display.print(idx == settingsSel ? "> " : "  ");
    display.println(setupItems[idx]);
  }
  display.display();
#else
  Serial.println("--- SETUP ---");
  for (int i = 0; i < SETUP_FIXED_COUNT; i++) {
    Serial.print(i == settingsSel ? "> " : "  ");
    Serial.println(setupItems[i]);
  }
#endif
}

// =============================================================================
// DIAGNOSTICS — live position + velocity readout. Click button to go back.
// =============================================================================
void drawDiagnostics() {
#ifndef NO_OLED
  // Snapshot the live motor state (read volatiles atomically)
  noInterrupts();
  long   pos      = motPosition;
  uint32_t interval = motInterval;
  bool   running  = (motionState != MOT_IDLE && motionState != MOT_DONE);
  interrupts();

  // Position in feet (2 decimals)
  float feet = pos / (STEPS_PER_INCH * 12.0f);

  // Velocity in inches/min. Current speed = TIMER_FREQ / interval (steps/sec).
  // in/min = (steps/sec / steps/in) * 60.  Zero when not moving.
  float inPerMin = 0.0f;
  if (running && interval > 0) {
    float stepsPerSec = (float)TIMER_FREQ / (float)interval;
    inPerMin = (stepsPerSec / STEPS_PER_INCH) * 60.0f;
  }

  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0, 0); display.println("== DIAGNOSTICS ==");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 16);
  display.print("Pos: "); display.print(feet, 2); display.println(" ft");

  display.setCursor(0, 26);
  display.print("Vel: "); display.print(inPerMin, 1); display.println(" in/min");

  // DIAG: last-stop reason + limit noise characterization.
  // reason: 1=zero 2=hardzero 3=stepdone 4=limit
  noInterrupts();
  uint8_t rsn = motLastStopReason;
  uint32_t maxStreak = motLimitMaxStreakMs;
  interrupts();
  const char* rsnStr = (rsn == 1) ? "zero" : (rsn == 2) ? "hardzero"
                     : (rsn == 3) ? "stepdone" : (rsn == 4) ? "limit" : "-";
  display.setCursor(0, 36);
  display.print("End: "); display.print(rsnStr);
  // Longest unbroken HIGH streak seen on the limit line this run (trip = 40ms).
  // If this creeps toward 40, noise is near the threshold.
  display.setCursor(0, 44);
  display.print("LimMax: "); display.print(maxStreak); display.print("ms");

  display.setCursor(0, 54);
  display.print("Click = back");
  display.display();
#else
  noInterrupts();
  long pos = motPosition;
  uint32_t interval = motInterval;
  bool running = (motionState != MOT_IDLE && motionState != MOT_DONE);
  interrupts();
  float feet = pos / (STEPS_PER_INCH * 12.0f);
  float inPerMin = (running && interval > 0)
    ? (((float)TIMER_FREQ / (float)interval) / STEPS_PER_INCH) * 60.0f : 0.0f;
  Serial.print("DIAG Pos:"); Serial.print(feet, 2);
  Serial.print("ft  Vel:"); Serial.print(inPerMin, 1); Serial.println("in/min");
#endif
}

void handleDiagnostics(int ticks, bool btnShort) {
  // The screen refreshes continuously from loop(); nothing to do on encoder.
  // A button press returns to the main menu.
  if (btnShort) {
    settingsPage = SET_NONE;
    drawMainMenu();
  }
}

void drawLearnScreen() {
  bool isA = (settingsPage == SET_LEARN_A);

#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("LEARN POS "); display.println(isA ? "A" : "B");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 14); display.print("Pos: "); display.print(stepsToFeet(currentPos)); display.println(" ft");
  display.setCursor(0, 26);
  display.print("A:"); display.print(posASet ? stepsToFeet(posA) : "---");
  display.print("  B:"); display.print(posBSet ? stepsToFeet(posB) : "---");
  // Direction indicator
  display.setCursor(0, 40);
  if (jogVelocity > 0) {
    display.println(">>> FORWARD >>>");
  } else if (jogVelocity < 0) {
    display.println("<<< REVERSE <<<");
  } else {
    display.println("    STOPPED");
  }
  display.setCursor(0, 54);
  display.print("CW=fwd CCW=rev ");
  display.print(isA ? "Btn=A" : "Btn=B");
  display.display();
#else
  Serial.print(isA ? "LEARN A" : "LEARN B");
  Serial.print("  Pos:"); Serial.print(stepsToFeet(currentPos)); Serial.print("ft");
  Serial.print("  ");
  if (jogVelocity > 0) Serial.println(">>FWD");
  else if (jogVelocity < 0) Serial.println("<<REV");
  else Serial.println("STOP");
#endif
}

void showSplash() {
#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(10, 8);  display.println("AXWORTHY GHOST");
  display.setCursor(10, 22); display.println("Pulley System v1.3");
  display.setCursor(10, 40); display.print("Mode: "); display.println(modeNames[runMode]);
  display.setCursor(10, 52);
  display.println((posASet && posBSet) ? "Positions loaded." : "No positions set.");
  display.display();
#else
  Serial.println("=== AXWORTHY GHOST v1.3 ===");
  Serial.print("Mode: "); Serial.println(modeNames[runMode]);
#endif
}

void showMessage(const char* l1, const char* l2, int ms) {
#ifndef NO_OLED
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(20, 20); display.println(l1);
  if (l2[0] != '\0') { display.setCursor(20, 36); display.println(l2); }
  display.display();
#else
  Serial.print(">> "); Serial.print(l1);
  if (l2[0] != '\0') { Serial.print(" "); Serial.print(l2); }
  Serial.println();
#endif
  delay(ms);
}


// =============================================================================
// TIMER-INTERRUPT MOTION SYSTEM
// Uses TC4 on SAMD21 for background step generation.
// Main loop remains fully responsive during motor travel.
// =============================================================================

void setupMotionTimer() {
  // Enable clock for TC4 (GCLK0 = 48MHz on the Arduino SAMD core; /64 = 750kHz)
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TC4_TC5;
  while (GCLK->STATUS.bit.SYNCBUSY);

  // Reset TC4
  TC4->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
  while (TC4->COUNT16.CTRLA.bit.SWRST);

  // Configure: 16-bit, match frequency mode, prescaler /64 (750kHz tick rate)
  TC4->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV64;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  // Set initial compare value (won't matter until we start a move)
  TC4->COUNT16.CC[0].reg = 60000;  // placeholder
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  // Enable overflow/match interrupt
  TC4->COUNT16.INTENSET.reg = TC_INTENSET_MC0;

  // Enable IRQ in NVIC
  NVIC_EnableIRQ(TC4_IRQn);
  NVIC_SetPriority(TC4_IRQn, 1);  // high priority (below encoder ISR)

  // Don't enable timer yet — it starts when a move begins
}

void startMove(long targetPos) {
  long delta = targetPos - currentPos;
  if (delta == 0) {
    motLastStopReason = 1;   // DIAG: zero-distance move
    motionComplete = true;
    return;
  }

  motFwd = (delta > 0);
  digitalWrite(DIR_PIN, motFwd ? HIGH : LOW);
  delayMicroseconds(5);

  motTotalSteps = abs(delta);
  motStepsDone = 0;
  motPosition = currentPos;
  motionComplete = false;

  // Calculate accel/decel step counts from ramp TIME
  // accelTime is in tenths of seconds. During ramp, speed goes from minSpeed to maxSpeed.
  // Average speed during ramp = (minSpeed + maxSpeed) / 2
  // Steps in ramp = averageSpeed * time
  float accelSec = accelTime / 10.0f;
  float decelSec = decelTime / 10.0f;
  float avgSpeed = ((float)maxSpeed + (float)minSpeed) / 2.0f;
  long fullAccelSteps = (long)(avgSpeed * accelSec);
  long fullDecelSteps = (long)(avgSpeed * decelSec);
  motAccelSteps = fullAccelSteps;
  long decelSteps = fullDecelSteps;
  
  // Scale ramp time proportionally if move is too short for full ramps
  float accelScale = 1.0f;
  float decelScale = 1.0f;
  if (motAccelSteps + decelSteps > motTotalSteps) {
    // Distribute proportionally
    float totalRamp = (float)(motAccelSteps + decelSteps);
    accelScale = (float)motTotalSteps / totalRamp;
    decelScale = accelScale;
    motAccelSteps = (long)(motAccelSteps * accelScale);
    decelSteps = motTotalSteps - motAccelSteps;
  }
  motDecelStart = motTotalSteps - decelSteps;
  
  // Calculate intervals (in timer ticks)
  motMinInterval = TIMER_FREQ / (uint32_t)maxSpeed;   // fastest (at cruise)
  motMaxInterval = TIMER_FREQ / (uint32_t)minSpeed;   // slowest (at start/end)
  if (motMaxInterval > 65000) motMaxInterval = 65000;  // safety clamp for 16-bit timer
  motInterval = motMaxInterval;  // start at min speed

  // Start in accel state
  motionState = MOT_ACCEL;

  // Set timer compare and enable
  TC4->COUNT16.CC[0].reg = (uint16_t)min(motInterval, 65535UL);
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
  TC4->COUNT16.COUNT.reg = 0;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);

  // Enable timer
  TC4->COUNT16.CTRLA.bit.ENABLE = 1;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
}

void stopMotion() {
  // Disable timer
  TC4->COUNT16.CTRLA.bit.ENABLE = 0;
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);
  motionState = MOT_IDLE;
  // Update currentPos from volatile counter
  noInterrupts();
  currentPos = motPosition;
  interrupts();
}

bool isMotionRunning() {
  return (motionState != MOT_IDLE && motionState != MOT_DONE);
}

// TC4 Interrupt Service Routine — generates step pulses
void TC4_Handler() {
  // Clear interrupt flag
  TC4->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;

  if (motionState == MOT_IDLE || motionState == MOT_DONE) {
    TC4->COUNT16.CTRLA.bit.ENABLE = 0;
    return;
  }

  // Generate step pulse using direct port access for speed.
  // On XIAO SAMD21, STEP = D0 = PA02 (Port A, bit 2).
  PORT->Group[g_APinDescription[STEP_PIN].ulPort].OUTSET.reg = 
    (1ul << g_APinDescription[STEP_PIN].ulPin);
  // ~1µs delay at 48MHz ≈ 48 NOPs. A4988 min STEP high is 1µs; keeping this
  // short frees CPU for loop() (button/encoder responsiveness) at 1/16 rates.
  __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
  __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
  __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
  __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
  __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
  __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
  PORT->Group[g_APinDescription[STEP_PIN].ulPort].OUTCLR.reg = 
    (1ul << g_APinDescription[STEP_PIN].ulPin);

  // Update position
  motPosition += motFwd ? 1 : -1;
  motStepsDone++;

  // Hard zero clamp in ISR — immediately stop if going negative
  if (!motFwd && motPosition <= 0) {
    motPosition = 0;
    motLastStopReason = 2;   // DIAG: hard-zero clamp
    motLastStopSteps  = motStepsDone;
    motLastStopTotal  = motTotalSteps;
    motionState = MOT_DONE;
    motionComplete = true;
    TC4->COUNT16.CTRLA.bit.ENABLE = 0;
    return;
  }

  // LIMIT E-STOP (in the ISR, so it works even during accel when loop() is
  // starved by the high step rate). The check is gated on WALL-CLOCK TIME
  // (millis), NOT step count — step-count gating made detection latency depend
  // on motor speed (it took ~2.5s at low speed). Time gating gives a consistent
  // latency at any speed. Noise rejection: require LIMIT_ISR_CONFIRMS consecutive
  // HIGH reads spaced LIMIT_CHECK_MS apart; a noise burst can't stay HIGH that
  // long, but a real mechanical press holds solid. ~LIMIT_ISR_CONFIRMS*
  // LIMIT_CHECK_MS total latency (a few ms).
  {
    static uint32_t lastLimCheckMs = 0;
    static uint8_t  limHighCount   = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastLimCheckMs >= LIMIT_CHECK_MS) {
      lastLimCheckMs = nowMs;
      if (PORT->Group[g_APinDescription[LIMIT_PIN].ulPort].IN.reg &
          (1ul << g_APinDescription[LIMIT_PIN].ulPin)) {
        limHighCount++;
        // DIAG: track the longest unbroken HIGH streak seen (even if it doesn't
        // trip) so we can see how close noise gets to the threshold.
        uint32_t streakMs = (uint32_t)limHighCount * LIMIT_CHECK_MS;
        if (streakMs > motLimitMaxStreakMs) motLimitMaxStreakMs = streakMs;
        if (limHighCount >= LIMIT_ISR_CONFIRMS) {
          motEstopHeldMs    = streakMs;   // DIAG: HIGH duration that caused the trip
          limHighCount = 0;
          motLastStopReason = 4;   // DIAG: limit E-stop (ISR)
          motLastStopSteps  = motStepsDone;
          motLastStopTotal  = motTotalSteps;
          motLimitTripped   = true;   // flag emergency stop (not a normal completion)
          motionState = MOT_DONE;
          motionComplete = true;
          TC4->COUNT16.CTRLA.bit.ENABLE = 0;
          return;
        }
      } else {
        limHighCount = 0;   // released or noise gap — reset
      }
    }
  }

  // Check if move complete
  if (motStepsDone >= motTotalSteps) {
    motLastStopReason = 3;   // DIAG: normal step-count complete
    motLastStopSteps  = motStepsDone;
    motLastStopTotal  = motTotalSteps;
    motionState = MOT_DONE;
    motionComplete = true;
    TC4->COUNT16.CTRLA.bit.ENABLE = 0;
    return;
  }

  // Skip speed calculation if we're in pure cruise mode (learn/jog mode)
  // motAccelSteps == 0 means no ramp — maintain current interval unchanged
  if (motAccelSteps == 0 && motDecelStart >= motTotalSteps - 1) {
    // Pure cruise — don't touch motInterval, it was set by handleLearnMode
    uint16_t compare = (uint16_t)min(motInterval, 65535UL);
    TC4->COUNT16.CC[0].reg = compare;
    return;
  }

  // Update speed/interval based on STEP COUNT (not elapsed time). Both accel and
  // decel use motStepsDone, which increments by exactly 1 per step, so the speed
  // ramp advances in uniform increments. (A previous version estimated elapsed
  // time by summing motInterval; at low speed those intervals are large, so the
  // ramp leaped forward in big chunks early on and the motor skipped mid-accel.)
  uint32_t speed;
  if (motStepsDone < motAccelSteps && motStepsDone < motDecelStart) {
    // Accelerating: speed ramps linearly over STEP COUNT (step-based, like decel).
    // Step count increments by exactly 1 per step, so progress rises in uniform
    // increments — no big early velocity leaps. (The old time-estimate summed
    // motInterval, which jumped in large chunks at low speed and caused the motor
    // to skip mid-ramp, especially with longer accel times.)
    float progress = (float)motStepsDone / (float)motAccelSteps;
    if (progress > 1.0f) progress = 1.0f;
    speed = (uint32_t)minSpeed + (uint32_t)(((float)maxSpeed - (float)minSpeed) * progress);
    motionState = MOT_ACCEL;
  } else if (motStepsDone >= motDecelStart) {
    // Decelerating: speed ramps down linearly over TIME
    long stepsIntoDecel = motStepsDone - motDecelStart;
    long decelLength = motTotalSteps - motDecelStart;
    float stepProgress = (float)stepsIntoDecel / (float)decelLength;
    // For decel, use step-based since we know exactly when to stop
    speed = (uint32_t)maxSpeed - (uint32_t)(((float)maxSpeed - (float)minSpeed) * stepProgress);
    motionState = MOT_DECEL;
  } else {
    // Cruising at max speed
    speed = (uint32_t)maxSpeed;
    motionState = MOT_CRUISE;
  }

  if (speed < (uint32_t)minSpeed) speed = (uint32_t)minSpeed;
  if (speed > (uint32_t)maxSpeed) speed = (uint32_t)maxSpeed;
  motInterval = TIMER_FREQ / speed;

  // Clamp to 16-bit timer range
  uint16_t compare = (uint16_t)min(motInterval, 65535UL);
  TC4->COUNT16.CC[0].reg = compare;
}


// =============================================================================
// ENCODER ISR + CONSUMER
// =============================================================================
void encoderISR() {
  unsigned long now = micros();
  if (now - lastEncTime < ENC_DEBOUNCE_US) return;
  lastEncTime = now;
  int clk = digitalRead(ENC_CLK_PIN);
  int dt  = digitalRead(ENC_DT_PIN);
  if (clk != lastEncCLK) {
    encDelta += (dt == clk) ? 1 : -1;
    lastEncCLK = clk;
  }
}

int consumeEncoderTicks() {
  noInterrupts();
  int t = encDelta; encDelta = 0;
  interrupts();
  return t;
}

// =============================================================================
// LIMIT SWITCH — glitch-filtered read
// =============================================================================
// The limit line (D8) is read raw in the main loop, where it feeds the
// screensaver-wake and the manual-home E-stop counter. Under 1/16 microstepping
// the A4988 switches ~2x as often as at 1/8, so more noise couples onto D8 and a
// single spurious HIGH sample can look like a limit trip. Requiring two HIGH
// reads a few hundred microseconds apart rejects single-sample glitches while
// adding negligible latency. Returns true only if the switch is really pressed.
bool readLimitDebounced() {
  if (digitalRead(LIMIT_PIN) != HIGH) return false;  // fast path: clearly released
  delayMicroseconds(300);
  return (digitalRead(LIMIT_PIN) == HIGH);            // confirm it held
}

// =============================================================================
// BUTTON — polled, noise-hardened, and designed to tolerate SPARSE calls.
// =============================================================================
// Polled by loop() (and the schedule/clock editors). During a run the step ISR
// starves loop(), so this is called infrequently/irregularly — the logic below
// is built to still catch a press under those conditions:
//
//   - OVERSAMPLE + majority vote: 5 reads, 3-of-5, rejects lone noise spikes.
//   - PRESS LATCH: the moment we see a confirmed pressed sample we latch
//     btnWasPressed=true and record the time. We do NOT require many consecutive
//     calls to "confirm" the press — a single sparse call that catches it is
//     enough to latch. This is what makes it work when loop() runs rarely.
//   - MIN PRESS DURATION: on the first call that sees it released again, we fire
//     only if the latched press lasted >= BTN_MIN_PRESS_MS (rejects noise blips;
//     a real tap easily exceeds it). Interaction unchanged: plain tap+release.
#define BTN_MIN_PRESS_MS   40    // press must be held at least this long to count

// Majority vote of 5 quick reads. true = pressed (LOW).
static bool btnVotePressed() {
  uint8_t low = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if (digitalRead(ENC_SW_PIN) == LOW) low++;
  }
  return (low >= 3);   // 3-of-5
}

// Returns true once per confirmed press, on the call that first sees release.
bool checkButton() {
  static unsigned long pressStartMs = 0;   // when the latched press began (0 = not pressed)

  bool pressed = btnVotePressed();

  if (pressed) {
    // Latch the press immediately on the first call that sees it (even if loop()
    // is being called sparsely during a run). Record when it started.
    if (pressStartMs == 0) pressStartMs = millis();
    btnWasPressed = true;
    return false;                 // don't fire until release
  } else {
    // Released. If we had a latched press, decide whether it was real.
    if (btnWasPressed) {
      btnWasPressed = false;
      unsigned long held = millis() - pressStartMs;
      pressStartMs = 0;
      if (held >= BTN_MIN_PRESS_MS) {
        return true;              // confirmed real press+release
      }
      // too short — noise, fire nothing
    }
    pressStartMs = 0;
    return false;
  }
}


// =============================================================================
// FLASH STORAGE
// =============================================================================
void saveFlash() {
  SavedData d;
  d.magic       = EEPROM_MAGIC_VAL;
  d.posASet     = posASet;    d.posBSet     = posBSet;
  d.posA        = posA;       d.posB        = posB;
  d.maxSpeed    = maxSpeed;   d.accelTime   = accelTime;   d.decelTime = decelTime;
  d.dwellSec    = dwellSec;   d.dwellMinSec = dwellMinSec; d.dwellMaxSec = dwellMaxSec;
  d.runMode     = runMode;    d.runState    = runState;
  d.schedStart  = schedStart; d.schedStartMin = schedStartMin;
  d.schedStop   = schedStop;  d.schedStopMin  = schedStopMin;
  flashStore.write(d);
}

void loadFlash() {
  SavedData d = flashStore.read();
  if (d.magic != EEPROM_MAGIC_VAL) return;
  posASet     = d.posASet;    posBSet     = d.posBSet;
  posA        = d.posA;       posB        = d.posB;
  maxSpeed    = constrain(d.maxSpeed,    170, 15300);
  accelTime   = constrain(d.accelTime,   1, 50);
  decelTime   = constrain(d.decelTime,   1, 50);
  dwellSec    = constrain(d.dwellSec,    0,   30);
  dwellMinSec = constrain(d.dwellMinSec, 0,   30);
  dwellMaxSec = constrain(d.dwellMaxSec, 1,   60);
  runMode     = constrain(d.runMode,     0,   1);
  runState    = constrain(d.runState,    0,   2);
  schedStart  = constrain(d.schedStart,  0,   23);
  schedStartMin = constrain(d.schedStartMin, 0, 59);
  schedStop   = constrain(d.schedStop,   0,   23);
  schedStopMin  = constrain(d.schedStopMin, 0, 59);
}

void updateActiveSettings() {
  if (runMode == MODE_WANDER) {
    activeSettings      = settingsWander;
    activeSettingsCount = SETTINGS_WANDER_COUNT;
  } else {
    activeSettings      = settingsFwdBack;
    activeSettingsCount = SETTINGS_FWDBACK_COUNT;
  }
}
