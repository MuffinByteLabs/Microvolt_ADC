/*
  =====================================================================
   MILLIVOLT MEASUREMENT AND ALARM SYSTEM                      v1.1
   Arduino UNO R3  +  ADS1256 24-bit ADC  +  microSD logging
  =====================================================================

   What this does (plain English)
   ------------------------------
   The loop-stick antenna produces a tiny DC voltage (thousandths of a
   volt = millivolts).  This sketch reads that voltage with a very
   precise 24-bit converter (the ADS1256), and runs the workflow the
   client described:

     Button 1  ->  start recording "air" readings every 0.5 s
                   (green LED flashes while recording)
     Button 2  ->  stop; average them -> this is the BASELINE
                   (green LED solid = ready)
     Button 3  ->  start SCANNING: read every 0.5 s, compare to baseline,
                   if the change is bigger than the threshold set by the
                   knob -> red LED + buzzer
     Button 4  ->  stop scanning (baseline is kept for the next scan)

   Every reading is printed to the Serial Monitor (115200 baud) and
   written to a CSV file on the microSD card (LOG_0001.CSV, LOG_0002.CSV,
   ... a new file every time the unit is switched on).

   You can also drive it from the Serial Monitor: type 1 2 3 4 to press
   the buttons, s for status, ? for help.

   Sounds
   ------
     power-on ........ three rising notes
     button accepted . short click-beep
     baseline set .... two beeps
     scan started .... one long beep
     scan stopped .... two falling notes
     ALARM ........... beep-beep-beep while the reading is outside the threshold
     error ........... fast beeps with the red LED flashing

   Wiring summary (full details in the wiring guide)
   -------------------------------------------------
     Arduino pin   ->  what
     D2            ->  Button 1 (other leg to GND)   start baseline
     D3            ->  Button 2 (other leg to GND)   stop baseline
     D4            ->  Button 3 (other leg to GND)   start scan
     D5            ->  Button 4 (other leg to GND)   stop scan
     D6            ->  Green LED  (+ leg, through 330 ohm to LED, LED - leg to GND)
     D7            ->  Red LED    (same)
     D8            ->  Buzzer module I/O pin  (module VCC -> 5V, GND -> GND)
     D9            <-  ADS1256 DRDY   ("data ready" signal from the ADC)
     D10           ->  ADS1256 CS     ("chip select")
     D11 (MOSI)    ->  ADS1256 DIN    and  SD breakout DI
     D12 (MISO)    <-  ADS1256 DOUT   and  SD breakout DO
     D13 (SCK)     ->  ADS1256 SCLK   and  SD breakout CLK
     A0            <-  Potentiometer middle pin (ends to 5V and GND)
     A1            ->  SD breakout CS
     A2            ->  ADS1256 PDWN  (also labelled SYNC on some boards)
     A3            ->  ADS1256 RST   (if the module has this pin; else leave unconnected)
     5V / GND      ->  ADS1256 5V/GND, SD breakout 5V/GND, buttons, pot, LEDs

     Antenna:  SMA centre pin -> 1 k ohm -> ADS1256 AIN0
               SMA shell      -> 1 k ohm -> ADS1256 AIN1
               10 nF capacitor between AIN0 and AIN1 (at the ADC)
               SMA shell also -> "2.5 V bias point" (two 10 k resistors
               between 5V and GND, 10 uF cap to GND).  This holds the
               floating antenna at a voltage the ADC can work with.

   The ADC and the SD card can be missing: the sketch says so at
   start-up and keeps running (with TEST_MODE 1 it simulates the ADC).
   Settings you may want to change are all in the SETTINGS block below.
  =====================================================================
*/

#include <SPI.h>
#include <SD.h>

// =====================================================================
//  SETTINGS  (the only block you normally need to touch)
// =====================================================================

// Set to 1 to run WITHOUT the ADS1256 connected.  The sketch then makes
// up a realistic antenna reading (about 0.070 mV with noise and slow
// drift, like the client's own data) so you can test the buttons, LEDs,
// buzzer, knob and SD card.  While SCANNING in test mode you can fake a
// "buried object" by HOLDING BUTTON 2, or by typing  a  in the Serial
// Monitor (toggles it on/off).  Set back to 0 for the real device.
#define TEST_MODE 1

// ---- Buzzer -------------------------------------------------------
// BUZZER_PASSIVE: 1 = passive buzzer or passive buzzer MODULE (needs a
//                     tone, silent on a steady level)
//                 0 = active buzzer (sounds by itself when switched on)
// BUZZER_IDLE_LEVEL: the pin level that keeps the buzzer SILENT.
//                 HIGH for "low-level trigger" modules, LOW for a plain
//                 buzzer or an active-high module.
// The module used in this build is a passive, low-level-trigger module.
#define BUZZER_PASSIVE     1
#define BUZZER_IDLE_LEVEL  HIGH
const unsigned int BUZZER_ALARM_HZ = 2500;   // alarm pitch (passive buzzers only)
const bool ALARM_BEEPING = true;             // true = beep-beep-beep, false = continuous tone
const unsigned int ALARM_BEEP_ON_MS = 120;
const unsigned int ALARM_BEEP_PERIOD_MS = 300;

// ---- Sampling -----------------------------------------------------
// How often a reading is taken and logged (client asked for 0.5 s).
const unsigned long SAMPLE_INTERVAL_MS = 500;

// ---- Alarm rule ---------------------------------------------------
// The knob has three positions (left / middle / right).
// Alarm when:   |reading - baseline|  >=  KNOB_FACTOR[position] * |baseline|
//
//   Client's design document:  1x, 2x, 3x of the baseline   -> {1.0, 2.0, 3.0}
//   Client's spreadsheet:      baseline + 10 %               -> {0.10, 0.20, 0.30}
//
// Change the three numbers below once the client confirms which he wants.
const float KNOB_FACTOR[3] = {1.0, 2.0, 3.0};

// true  = alarm when the reading moves away from the baseline in EITHER
//         direction (this is what the design document says: |delta|).
// false = alarm only when the reading goes ABOVE the baseline.
const bool ALARM_BOTH_DIRECTIONS = true;

// Safety floor for the threshold, in mV.  If the baseline happens to be
// ~0 (for example inputs shorted on the bench) the threshold would be 0
// and every sample would alarm.  0.001 mV = 1 microvolt.
const float MIN_THRESHOLD_MV = 0.001;

// ---- Indicators ---------------------------------------------------
// Flash the green LED while the baseline is being recorded (solid = ready).
const bool BASELINE_BLINK_GREEN = true;

// ---- ADC ----------------------------------------------------------
// ADC reference voltage.  The module has an ADR03 = 2.500 V reference.
// If you measure the module's VREF pin with a good meter, put the exact
// value here for best absolute accuracy.
const float ADC_VREF_VOLTS = 2.500;

// Serial speed for the Serial Monitor (set the monitor to the same value).
const unsigned long SERIAL_BAUD = 115200;

// =====================================================================
//  PIN MAP
// =====================================================================
const uint8_t PIN_BTN_START_BASELINE = 2;
const uint8_t PIN_BTN_STOP_BASELINE  = 3;
const uint8_t PIN_BTN_START_SCAN     = 4;
const uint8_t PIN_BTN_STOP_SCAN      = 5;
const uint8_t PIN_LED_GREEN          = 6;
const uint8_t PIN_LED_RED            = 7;
const uint8_t PIN_BUZZER             = 8;
const uint8_t PIN_ADC_DRDY           = 9;
const uint8_t PIN_ADC_CS             = 10;
// D11 = MOSI, D12 = MISO, D13 = SCK  (fixed by the UNO hardware)
const uint8_t PIN_POT                = A0;
const uint8_t PIN_SD_CS              = A1;
const uint8_t PIN_ADC_PDWN           = A2;
const uint8_t PIN_ADC_RESET          = A3;

// =====================================================================
//  TYPES  (defined up here because the Arduino IDE needs them before
//  it generates the function prototypes)
// =====================================================================
struct Button {
  uint8_t pin;
  bool stableState;        // debounced state (HIGH = not pressed, because of the pull-up)
  bool lastReading;
  unsigned long lastChangeMs;
};

enum Phase { IDLE, BASELINE_RECORDING, SCANNING };

// =====================================================================
//  BUZZER  (works for active and passive buzzers, either trigger level)
// =====================================================================
void buzzerSilent() {
#if BUZZER_PASSIVE
  noTone(PIN_BUZZER);                       // noTone() leaves the pin LOW ...
#endif
  digitalWrite(PIN_BUZZER, BUZZER_IDLE_LEVEL);   // ... so park it at the silent level
}

void buzzerSound(unsigned int hz) {
#if BUZZER_PASSIVE
  tone(PIN_BUZZER, hz);
#else
  (void)hz;
  digitalWrite(PIN_BUZZER, BUZZER_IDLE_LEVEL == HIGH ? LOW : HIGH);
#endif
}

// Short blocking beep for feedback sounds (kept short so sampling is not disturbed)
void beep(unsigned int hz, unsigned int ms) {
  buzzerSound(hz);
  delay(ms);
  buzzerSilent();
}

void soundPowerOn()      { beep(1500, 80); delay(40); beep(2000, 80); delay(40); beep(2500, 120); }
void soundClick()        { beep(2000, 35); }
void soundBaselineSet()  { beep(2500, 90); delay(70); beep(2500, 90); }
void soundScanStart()    { beep(2200, 250); }
void soundScanStop()     { beep(2000, 90); delay(50); beep(1500, 140); }
void soundError()        { for (uint8_t i = 0; i < 4; i++) { beep(3000, 60); delay(60); } }

// =====================================================================
//  ADS1256 DRIVER  (small, self-contained; no library needed)
// =====================================================================
// Commands (from the ADS1256 datasheet, Table 24)
const uint8_t ADS_CMD_WAKEUP  = 0x00;
const uint8_t ADS_CMD_RDATA   = 0x01;   // read one conversion result
const uint8_t ADS_CMD_SDATAC  = 0x0F;   // stop continuous mode
const uint8_t ADS_CMD_RREG    = 0x10;   // read register(s)
const uint8_t ADS_CMD_WREG    = 0x50;   // write register(s)
const uint8_t ADS_CMD_SELFCAL = 0xF0;   // self-calibrate offset + gain
const uint8_t ADS_CMD_RESET   = 0xFE;

// Registers
const uint8_t ADS_REG_STATUS = 0x00;
const uint8_t ADS_REG_MUX    = 0x01;
const uint8_t ADS_REG_ADCON  = 0x02;
const uint8_t ADS_REG_DRATE  = 0x03;

// Register values we use
const uint8_t ADS_STATUS_VAL = 0x02;  // MSB first, auto-cal off, INPUT BUFFER ON (high impedance)
const uint8_t ADS_MUX_VAL    = 0x01;  // positive input = AIN0, negative input = AIN1
const uint8_t ADS_ADCON_VAL  = 0x06;  // clock-out off, sensor-detect off, PGA gain = 64
const uint8_t ADS_DRATE_VAL  = 0x23;  // 10 samples per second (rejects 50 Hz and 60 Hz mains hum)
const float   ADC_PGA        = 64.0;  // must match ADS_ADCON_VAL

// ADS1256 SPI: 1 MHz, MSB first, "mode 1" (clock idles low, data read on falling edge)
SPISettings adsSpiSettings(1000000, MSBFIRST, SPI_MODE1);

void adsSelect() {
  SPI.beginTransaction(adsSpiSettings);
  digitalWrite(PIN_ADC_CS, LOW);
  delayMicroseconds(2);
}

void adsDeselect() {
  delayMicroseconds(2);
  digitalWrite(PIN_ADC_CS, HIGH);
  SPI.endTransaction();
}

void adsCommand(uint8_t cmd) {
  adsSelect();
  SPI.transfer(cmd);
  delayMicroseconds(10);
  adsDeselect();
}

// Write 'count' consecutive registers starting at 'firstReg'
void adsWriteRegisters(uint8_t firstReg, const uint8_t *values, uint8_t count) {
  adsSelect();
  SPI.transfer(ADS_CMD_WREG | firstReg);
  SPI.transfer(count - 1);
  for (uint8_t i = 0; i < count; i++) SPI.transfer(values[i]);
  adsDeselect();
}

uint8_t adsReadRegister(uint8_t reg) {
  adsSelect();
  SPI.transfer(ADS_CMD_RREG | reg);
  SPI.transfer(0x00);            // number of registers - 1  (= just one)
  delayMicroseconds(10);         // datasheet t6: wait 50 clock cycles (6.5 us) before data
  uint8_t v = SPI.transfer(0x00);
  adsDeselect();
  return v;
}

// Wait until the ADC says "data ready" (DRDY pin goes LOW).  Returns false on timeout.
bool adsWaitForDataReady(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (digitalRead(PIN_ADC_DRDY) == HIGH) {
    if (millis() - t0 > timeoutMs) return false;
  }
  return true;
}

// Read one 24-bit conversion.  Only call when DRDY is LOW.
long adsReadConversion() {
  adsSelect();
  SPI.transfer(ADS_CMD_RDATA);
  delayMicroseconds(10);         // t6
  uint32_t v = (uint32_t)SPI.transfer(0x00) << 16;
  v |= (uint32_t)SPI.transfer(0x00) << 8;
  v |= (uint32_t)SPI.transfer(0x00);
  adsDeselect();
  if (v & 0x800000UL) v |= 0xFF000000UL;   // extend the sign bit (24-bit -> 32-bit)
  return (long)v;
}

// Convert a raw 24-bit count to millivolts.
// Full-scale input = +/- 2 * Vref / PGA  and that corresponds to +/- 8388607 counts.
float adsCountsToMillivolts(long counts) {
  const float fullScaleVolts = 2.0 * ADC_VREF_VOLTS / ADC_PGA;    // 0.078125 V at PGA 64
  return (float)counts * (fullScaleVolts / 8388607.0) * 1000.0;
}

// Power up, configure and self-calibrate the ADC.  Returns true if the
// chip answered correctly (we read back the registers we wrote).
bool adsInitialise() {
  digitalWrite(PIN_ADC_RESET, HIGH);
  digitalWrite(PIN_ADC_PDWN, LOW);      // hard power-down ...
  delay(20);
  digitalWrite(PIN_ADC_PDWN, HIGH);     // ... and back up = clean start
  delay(50);

  adsCommand(ADS_CMD_RESET);
  delay(10);
  adsCommand(ADS_CMD_SDATAC);           // make sure it is not streaming data
  delay(2);

  const uint8_t cfg[4] = {ADS_STATUS_VAL, ADS_MUX_VAL, ADS_ADCON_VAL, ADS_DRATE_VAL};
  adsWriteRegisters(ADS_REG_STATUS, cfg, 4);
  delay(2);

  uint8_t mux   = adsReadRegister(ADS_REG_MUX);
  uint8_t adcon = adsReadRegister(ADS_REG_ADCON);
  uint8_t drate = adsReadRegister(ADS_REG_DRATE);

  Serial.print(F("ADS1256 registers read back: MUX=0x"));  Serial.print(mux, HEX);
  Serial.print(F(" ADCON=0x"));                            Serial.print(adcon, HEX);
  Serial.print(F(" DRATE=0x"));                            Serial.println(drate, HEX);

  if (mux != ADS_MUX_VAL || adcon != ADS_ADCON_VAL || drate != ADS_DRATE_VAL) {
    return false;   // the chip is not talking to us (check wiring / power)
  }

  adsCommand(ADS_CMD_SELFCAL);          // remove the ADC's own offset and gain error
  delay(5);
  if (!adsWaitForDataReady(3000)) return false;
  return true;
}

// =====================================================================
//  BUTTONS  (with debounce: a press is only counted once)
// =====================================================================
Button btnStartBaseline = {PIN_BTN_START_BASELINE, HIGH, HIGH, 0};
Button btnStopBaseline  = {PIN_BTN_STOP_BASELINE,  HIGH, HIGH, 0};
Button btnStartScan     = {PIN_BTN_START_SCAN,     HIGH, HIGH, 0};
Button btnStopScan      = {PIN_BTN_STOP_SCAN,      HIGH, HIGH, 0};

const unsigned long DEBOUNCE_MS = 30;

// Returns true exactly once each time the button is pressed down.
bool buttonWasPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) {
    b.lastChangeMs = millis();
    b.lastReading = reading;
  }
  if ((millis() - b.lastChangeMs) > DEBOUNCE_MS && reading != b.stableState) {
    b.stableState = reading;
    if (b.stableState == LOW) return true;   // HIGH -> LOW = pressed
  }
  return false;
}

// =====================================================================
//  KNOB (potentiometer) -> position 0, 1 or 2   with a little hysteresis
// =====================================================================
uint8_t knobPosition = 0;

uint8_t readKnobPosition() {
  int v = 0;
  for (uint8_t i = 0; i < 8; i++) v += analogRead(PIN_POT);
  v /= 8;                                            // 0 .. 1023
  uint8_t candidate = (v < 341) ? 0 : (v < 682) ? 1 : 2;
  const int HYST = 12;                               // stops flicker at the boundaries
  if (candidate > knobPosition && v >= (int)(knobPosition + 1) * 341 + HYST) knobPosition = candidate;
  if (candidate < knobPosition && v <  (int)knobPosition * 341 - HYST)       knobPosition = candidate;
  return knobPosition;
}

// =====================================================================
//  SD CARD LOGGING
// =====================================================================
File logFile;
bool sdReady = false;
char logFileName[13] = "LOG_0001.CSV";

bool sdStart() {
  if (!SD.begin(PIN_SD_CS)) return false;
  // find the first file name that does not exist yet
  for (unsigned int n = 1; n <= 9999; n++) {
    snprintf(logFileName, sizeof(logFileName), "LOG_%04u.CSV", n);
    if (!SD.exists(logFileName)) break;
  }
  logFile = SD.open(logFileName, FILE_WRITE);
  if (!logFile) return false;
  logFile.println(F("time_s,phase,sample,reading_mV,baseline_mV,delta_mV,knob_pos,knob_factor,threshold_mV,alarm"));
  logFile.flush();
  return true;
}

// =====================================================================
//  SYSTEM STATE
// =====================================================================
Phase phase = IDLE;

bool  baselineValid   = false;
float baselineMv      = 0.0;
double baselineSum    = 0.0;      // running sum during air recording
unsigned long sampleNumber = 0;   // counts samples in the current phase
unsigned long lastScanTotal = 0;  // samples in the most recent scan

// Readings from the ADC are collected continuously (10 per second) and
// averaged into one value every SAMPLE_INTERVAL_MS.
double accSum = 0.0;
unsigned int accCount = 0;
unsigned long lastSampleMs = 0;
unsigned long lastIdlePrintMs = 0;
bool adcOk = false;
float lastReadingMv = 0.0;

bool alarmActive = false;         // true while the last scan sample was outside the threshold
#if TEST_MODE
bool simulatedAnomaly = false;    // toggled with 'a' in the Serial Monitor
#endif

// ---------------------------------------------------------------------
void setLeds(bool green, bool red) {
  digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,   red   ? HIGH : LOW);
}

void printMv(Print &out, float mv) { out.print(mv, 4); }

void printKnob() {
  Serial.print(F("Knob position ")); Serial.print(knobPosition + 1);
  Serial.print(F(" of 3  ->  threshold factor ")); Serial.print(KNOB_FACTOR[knobPosition], 2); Serial.print('x');
  if (baselineValid) {
    float thr = KNOB_FACTOR[knobPosition] * fabs(baselineMv);
    if (thr < MIN_THRESHOLD_MV) thr = MIN_THRESHOLD_MV;
    Serial.print(F("  ->  alarm if |delta| >= ")); printMv(Serial, thr); Serial.print(F(" mV"));
  }
  Serial.println();
}

void printHelp() {
  Serial.println(F("Buttons:  1 = start baseline   2 = stop baseline   3 = start scan   4 = stop scan"));
  Serial.println(F("Serial:   type 1 2 3 4 to press a button,  s = status,  ? = this help"));
#if TEST_MODE
  Serial.println(F("          a = toggle simulated buried object (test mode), or hold Button 2 while scanning"));
#endif
}

void printStatus() {
  Serial.println(F("---------------- STATUS ----------------"));
  Serial.print(F("State      : "));
  Serial.println(phase == IDLE ? F("IDLE") : phase == BASELINE_RECORDING ? F("RECORDING BASELINE") : F("SCANNING"));
  Serial.print(F("Baseline   : "));
  if (baselineValid) { printMv(Serial, baselineMv); Serial.println(F(" mV")); } else Serial.println(F("none yet"));
  Serial.print(F("Last read  : ")); printMv(Serial, lastReadingMv); Serial.println(F(" mV"));
  printKnob();
  Serial.print(F("Samples    : ")); Serial.print(sampleNumber); Serial.println(F(" in the current phase"));
  Serial.print(F("Alarm      : ")); Serial.println(alarmActive ? F("ON") : F("off"));
  Serial.print(F("SD card    : ")); if (sdReady) Serial.println(logFileName); else Serial.println(F("not present (Serial only)"));
  Serial.print(F("ADC        : "));
#if TEST_MODE
  Serial.println(F("SIMULATED (TEST_MODE 1)"));
#else
  Serial.println(adcOk ? F("ADS1256 OK") : F("not responding"));
#endif
  Serial.print(F("Up time    : ")); Serial.print(millis() / 1000); Serial.println(F(" s"));
  Serial.println(F("----------------------------------------"));
}

// One log line to Serial AND to the SD card.
void logRow(const __FlashStringHelper *phaseName, unsigned long n, float mv,
            bool haveDelta, float delta, uint8_t knob, float thr, bool alarm) {
  float t = millis() / 1000.0;

  // ---- Serial (human readable) ----
  Serial.print(t, 1);           Serial.print(F(" s  "));
  Serial.print(phaseName);      Serial.print(F("  #"));
  Serial.print(n);              Serial.print(F("  reading="));
  printMv(Serial, mv);          Serial.print(F(" mV"));
  if (baselineValid) {
    Serial.print(F("  baseline=")); printMv(Serial, baselineMv); Serial.print(F(" mV"));
  }
  if (haveDelta) {
    Serial.print(F("  delta="));    printMv(Serial, delta);    Serial.print(F(" mV"));
    Serial.print(F("  knob="));     Serial.print(knob + 1);
    Serial.print(F(" ("));          Serial.print(KNOB_FACTOR[knob], 2); Serial.print(F("x)"));
    Serial.print(F("  threshold=")); printMv(Serial, thr);     Serial.print(F(" mV"));
    Serial.print(alarm ? F("  *** ALARM ***") : F("  ok"));
  }
  Serial.println();

  // ---- SD card (CSV) ----
  if (sdReady) {
    logFile.print(t, 1);          logFile.print(',');
    logFile.print(phaseName);     logFile.print(',');
    logFile.print(n);             logFile.print(',');
    logFile.print(mv, 5);         logFile.print(',');
    if (baselineValid) logFile.print(baselineMv, 5);
    logFile.print(',');
    if (haveDelta) {
      logFile.print(delta, 5);    logFile.print(',');
      logFile.print(knob + 1);    logFile.print(',');
      logFile.print(KNOB_FACTOR[knob], 3); logFile.print(',');
      logFile.print(thr, 5);      logFile.print(',');
      logFile.print(alarm ? 1 : 0);
    } else {
      logFile.print(F(",,,,"));
    }
    logFile.println();
    logFile.flush();              // make sure it is really on the card (battery could be pulled)
  }
}

// A free-text event line (start/stop messages) to Serial and SD.
//   valueKind: 0 = no value, 1 = value is millivolts, 2 = value is a sample count
void logEvent(const __FlashStringHelper *text, float value, uint8_t valueKind) {
  Serial.print(F("---- ")); Serial.print(text);
  if (valueKind == 1) { Serial.print(' '); printMv(Serial, value); Serial.print(F(" mV")); }
  if (valueKind == 2) { Serial.print(F(", total samples = ")); Serial.print((unsigned long)value); }
  Serial.println(F(" ----"));
  if (sdReady) {
    logFile.print(millis() / 1000.0, 1); logFile.print(F(",EVENT,"));
    if (valueKind == 2) logFile.print((unsigned long)value);     // sample column = total count
    logFile.print(',');
    if (valueKind == 1) logFile.print(value, 5);                  // reading column = baseline mV
    logFile.print(F(",,,,,,")); logFile.print(text); logFile.println();
    logFile.flush();
  }
}

// =====================================================================
//  THE FOUR ACTIONS  (called by the buttons AND by Serial commands)
// =====================================================================
void actionStartBaseline() {
  soundClick();
  phase = BASELINE_RECORDING;
  baselineValid = false;
  alarmActive = false; buzzerSilent();
  baselineSum = 0.0;
  sampleNumber = 0;
  accSum = 0.0; accCount = 0; lastSampleMs = millis();
  setLeds(false, false);
  logEvent(F("BASELINE RECORDING STARTED - hold the antenna in the air and keep still"), 0, 0);
  Serial.println(F("Recording... press Button 2 when you have enough readings (20 s = about 40 readings)."));
}

void actionStopBaseline() {
  if (phase != BASELINE_RECORDING) {
    Serial.println(F("(Button 2 does nothing right now - it stops a baseline recording. Press Button 1 first.)"));
    return;
  }
  phase = IDLE;
  if (sampleNumber > 0) {
    baselineMv = baselineSum / sampleNumber;
    baselineValid = true;
    setLeds(true, false);                        // green solid = ready
    Serial.print(F("Baseline = average of ")); Serial.print(sampleNumber); Serial.println(F(" readings:"));
    logEvent(F("BASELINE SET"), baselineMv, 1);
    printKnob();
    Serial.println(F("Green LED on. Press Button 3 to start scanning."));
    soundBaselineSet();
  } else {
    setLeds(false, false);
    logEvent(F("BASELINE STOPPED - no readings were taken - no baseline set"), 0, 0);
    soundClick();
  }
}

void actionStartScan() {
  if (!baselineValid) {
    Serial.println(F("No baseline yet. Press Button 1, wait, then Button 2 first."));
    soundError();
    return;
  }
  if (phase == SCANNING) {
    Serial.println(F("(Already scanning.)"));
    return;
  }
  phase = SCANNING;
  sampleNumber = 0;
  alarmActive = false;
  accSum = 0.0; accCount = 0; lastSampleMs = millis();
  setLeds(true, false);
  logEvent(F("SOIL SCAN STARTED - baseline"), baselineMv, 1);
  printKnob();
  soundScanStart();
}

void actionStopScan() {
  if (phase != SCANNING) {
    Serial.println(F("(Button 4 does nothing right now - it stops a scan. Press Button 3 to start one.)"));
    return;
  }
  phase = IDLE;
  alarmActive = false; buzzerSilent();
  lastScanTotal = sampleNumber;
  setLeds(true, false);                          // alarm off, green stays on (baseline kept)
  logEvent(F("SOIL SCAN STOPPED"), (float)sampleNumber, 2);
  Serial.println(F("Idle. Button 3 = scan again with the same baseline, Button 1 = record a new baseline."));
  soundScanStop();
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  // Buzzer first: a low-trigger module would otherwise sound while the pin floats
  pinMode(PIN_BUZZER, OUTPUT);
  buzzerSilent();

  // Chip-select lines HIGH so the two SPI devices stay quiet
  pinMode(PIN_ADC_CS, OUTPUT);  digitalWrite(PIN_ADC_CS, HIGH);
  pinMode(PIN_SD_CS,  OUTPUT);  digitalWrite(PIN_SD_CS,  HIGH);
  pinMode(PIN_ADC_DRDY, INPUT);
  pinMode(PIN_ADC_PDWN, OUTPUT); digitalWrite(PIN_ADC_PDWN, HIGH);
  pinMode(PIN_ADC_RESET, OUTPUT); digitalWrite(PIN_ADC_RESET, HIGH);

  pinMode(PIN_BTN_START_BASELINE, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP_BASELINE,  INPUT_PULLUP);
  pinMode(PIN_BTN_START_SCAN,     INPUT_PULLUP);
  pinMode(PIN_BTN_STOP_SCAN,      INPUT_PULLUP);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED,   OUTPUT);
  setLeds(false, false);

  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F(" Millivolt Measurement & Alarm System  v1.1"));
  Serial.println(F("=============================================="));
  Serial.print(F("Sample interval : ")); Serial.print(SAMPLE_INTERVAL_MS); Serial.println(F(" ms"));
  Serial.print(F("Knob factors    : ")); Serial.print(KNOB_FACTOR[0], 2); Serial.print(F("x / "));
  Serial.print(KNOB_FACTOR[1], 2); Serial.print(F("x / ")); Serial.print(KNOB_FACTOR[2], 2); Serial.println(F("x of |baseline|"));
  Serial.print(F("Alarm direction : ")); Serial.println(ALARM_BOTH_DIRECTIONS ? F("either direction") : F("above baseline only"));
#if TEST_MODE
  Serial.println(F("*** TEST MODE: the ADC is simulated (about 0.070 mV with noise). ***"));
#endif

  // Power-on self test: LEDs and a short rising jingle
  setLeds(true, true);
  soundPowerOn();
  setLeds(false, false);

  SPI.begin();

  // ---- SD card ----
  sdReady = sdStart();
  if (sdReady) {
    Serial.print(F("SD card OK, logging to ")); Serial.println(logFileName);
  } else {
    Serial.println(F("No SD card found - continuing with Serial logging only."));
    for (uint8_t i = 0; i < 3; i++) { digitalWrite(PIN_LED_RED, HIGH); delay(120); digitalWrite(PIN_LED_RED, LOW); delay(120); }
  }

  // ---- ADC ----
#if TEST_MODE
  adcOk = true;
#else
  adcOk = adsInitialise();
  while (!adcOk) {
    Serial.println(F("ERROR: ADS1256 not responding. Check 5V/GND, DIN/DOUT/SCLK/CS/DRDY wiring. Retrying..."));
    digitalWrite(PIN_LED_RED, HIGH); soundError(); digitalWrite(PIN_LED_RED, LOW);
    delay(1500);
    adcOk = adsInitialise();
  }
  Serial.println(F("ADS1256 OK (PGA 64, 10 SPS, buffer on, differential AIN0-AIN1, self-calibrated)"));
#endif

  Serial.println(F("Ready."));
  printHelp();
  knobPosition = 0;
  readKnobPosition();
  printKnob();
  lastSampleMs = millis();
}

// =====================================================================
//  ONE ADC READING (real or simulated), collected whenever one is ready
// =====================================================================
void collectAdcReading() {
#if TEST_MODE
  // Simulated antenna: ~0.070 mV like the client's data, with +/-0.005 mV
  // noise and a slow drift, 10 readings per second.
  static unsigned long lastFakeMs = 0;
  static float anomalyLevel = 0.0;                        // ramps up/down so it looks real
  if (millis() - lastFakeMs < 100) return;
  lastFakeMs = millis();
  float drift = 0.004 * sin(millis() / 30000.0);           // +/-0.004 mV over about 3 minutes
  float noise = random(-50, 51) / 10000.0;                 // +/-0.005 mV
  bool anomaly = (phase == SCANNING) && (simulatedAnomaly || digitalRead(PIN_BTN_STOP_BASELINE) == LOW);
  anomalyLevel += ((anomaly ? 0.150 : 0.0) - anomalyLevel) * 0.25;   // smooth ramp
  accSum += 0.070 + drift + noise + anomalyLevel;
  accCount++;
#else
  // If the loop was stalled (e.g. a slow SD-card write) we might land in the
  // brief moment where the ADC is updating its result register; reading then
  // could give garbage.  After any stall, read one conversion and THROW IT
  // AWAY: that resets DRDY, so the next LOW is guaranteed to be fresh data.
  static unsigned long lastPollMs = 0;
  static bool discardNext = false;
  unsigned long now = millis();
  if (now - lastPollMs > 50) discardNext = true;
  lastPollMs = now;

  if (digitalRead(PIN_ADC_DRDY) == LOW) {
    long counts = adsReadConversion();
    if (discardNext) { discardNext = false; return; }
    accSum += adsCountsToMillivolts(counts);
    accCount++;
  }
#endif
}

// =====================================================================
//  ALARM SOUND  (non-blocking beep pattern while alarmActive)
// =====================================================================
void updateAlarmSound() {
  static bool sounding = false;
  if (!alarmActive) {
    if (sounding) { buzzerSilent(); sounding = false; }
    return;
  }
  bool wantOn = ALARM_BEEPING ? ((millis() % ALARM_BEEP_PERIOD_MS) < ALARM_BEEP_ON_MS) : true;
  if (wantOn && !sounding)  { buzzerSound(BUZZER_ALARM_HZ); sounding = true; }
  if (!wantOn && sounding)  { buzzerSilent(); sounding = false; }
}

// =====================================================================
//  SERIAL COMMANDS  (so the unit can be driven from the Serial Monitor)
// =====================================================================
void handleSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '1': actionStartBaseline(); break;
      case '2': actionStopBaseline();  break;
      case '3': actionStartScan();     break;
      case '4': actionStopScan();      break;
      case 's': case 'S': printStatus(); break;
      case '?': case 'h': case 'H': printHelp(); break;
#if TEST_MODE
      case 'a': case 'A':
        simulatedAnomaly = !simulatedAnomaly;
        Serial.print(F("Simulated buried object: ")); Serial.println(simulatedAnomaly ? F("ON") : F("off"));
        break;
#endif
      default: break;   // ignore newlines and anything else
    }
  }
}

// =====================================================================
//  MAIN LOOP
// =====================================================================
void loop() {
  collectAdcReading();
  updateAlarmSound();
  handleSerialCommands();

  // ---- knob: report changes ----
  uint8_t before = knobPosition;
  readKnobPosition();
  if (knobPosition != before) printKnob();

  // ---- buttons ----
  if (buttonWasPressed(btnStartBaseline)) actionStartBaseline();
  if (buttonWasPressed(btnStopBaseline))  actionStopBaseline();
  if (buttonWasPressed(btnStartScan))     actionStartScan();
  if (buttonWasPressed(btnStopScan))      actionStopScan();

  // ---- green LED flashes while recording the baseline ----
  if (phase == BASELINE_RECORDING && BASELINE_BLINK_GREEN) {
    digitalWrite(PIN_LED_GREEN, (millis() % 1000) < 150 ? HIGH : LOW);
  }

  // ---- every SAMPLE_INTERVAL_MS: turn the collected readings into one sample ----
  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();

    if (accCount == 0) {
      if (phase != IDLE) Serial.println(F("WARNING: no data from ADC in this interval (check DRDY wire)"));
      return;
    }
    float mv = accSum / accCount;
    accSum = 0.0; accCount = 0;
    lastReadingMv = mv;

    if (phase == BASELINE_RECORDING) {
      sampleNumber++;
      baselineSum += mv;
      logRow(F("AIR"), sampleNumber, mv, false, 0, 0, 0, false);
    }
    else if (phase == SCANNING) {
      sampleNumber++;
      float delta = mv - baselineMv;
      uint8_t knob = knobPosition;
      float threshold = KNOB_FACTOR[knob] * fabs(baselineMv);
      if (threshold < MIN_THRESHOLD_MV) threshold = MIN_THRESHOLD_MV;
      bool alarm = ALARM_BOTH_DIRECTIONS ? (fabs(delta) >= threshold) : (delta >= threshold);
      alarmActive = alarm;                       // buzzer pattern follows this (see updateAlarmSound)
      setLeds(true, alarm);                      // red LED follows the alarm
      logRow(F("SCAN"), sampleNumber, mv, true, delta, knob, threshold, alarm);
    }
    else {
      // IDLE: show a live reading every 2 s so you can see it working on the bench
      if (millis() - lastIdlePrintMs >= 2000) {
        lastIdlePrintMs = millis();
        Serial.print(F("idle  live reading = ")); printMv(Serial, mv); Serial.println(F(" mV"));
      }
    }
  }
}


