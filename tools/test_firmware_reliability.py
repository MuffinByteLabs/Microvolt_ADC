"""Run host behavior tests against functions extracted from the current sketch.

The C++ under test is taken from the firmware on every run. Hardware, Print,
and the SD backend are mocked to exercise button timing, indicators, and
reported storage failures. This does not validate physical media or wiring.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
INO = ROOT / "firmware/Millivolt_Monitor/Millivolt_Monitor.ino"


def masked(source: str) -> str:
    """Hide comments/literals without moving the braces in executable C++."""
    pattern = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: "".join("\n" if c == "\n" else " " for c in m[0]),
                  source, flags=re.DOTALL)


def block(source: str, start_pattern: str, *, semicolon: bool = False) -> str:
    clean = masked(source)
    match = re.search(start_pattern, clean, re.MULTILINE)
    if not match:
        raise AssertionError(f"Missing firmware definition: {start_pattern}")
    opening = clean.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth and end < len(clean):
        depth += (clean[end] == "{") - (clean[end] == "}")
        end += 1
    if depth:
        raise AssertionError("Unbalanced firmware braces")
    if semicolon:
        end = clean.index(";", end) + 1
    return source[match.start():end]


def function(source: str, name: str) -> str:
    return block(source, rf"^[^\n;{{}}]*\b{re.escape(name)}\([^;{{}}]*\)\s*{{")


def constant(source: str, name: str) -> str:
    match = re.search(rf"^const\s+[^;\n]*\b{re.escape(name)}\s*=.*?;", source,
                      re.MULTILINE | re.DOTALL)
    if not match:
        raise AssertionError(f"Missing firmware constant: {name}")
    return match[0]


MOCKS = r'''
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using __FlashStringHelper = char;
#define F(x) (x)
constexpr int HIGH = 1, LOW = 0, SPI_HALF_SPEED = 1, A1 = 15;
constexpr uint8_t O_READ = 1, O_WRITE = 2, O_APPEND = 4, O_TRUNC = 16;
constexpr uint8_t O_CREAT = 64, O_EXCL = 128;
uint32_t nowMs = 0;
int pins[32] = {};
uint32_t millis() { return nowMs; }
int digitalRead(uint8_t pin) { return pins[pin]; }
void digitalWrite(uint8_t pin, int value) { pins[pin] = value; }
struct Io {
  int writes = 0, syncs = 0, closes = 0, mounts = 0;
  int probes = 0, createAttempts = 0, watchdogKicks = 0;
  int failWriteAt = -1, failMountStage = 0;
  bool failSync = false, failClose = false, failProbeIo = false, failCreate = false;
  uint8_t cardError = 0, lastCreateMode = 0;
  std::string ambiguousProbe;
  std::map<std::string, std::string> files;
} io;
void watchdogKick() { ++io.watchdogKicks; }
class Print {
  int error = 0;
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  int getWriteError() const { return error; }
  void setWriteError(int value = 1) { error = value; }
  void clearWriteError() { error = 0; }
  size_t print(const char *s) {
    size_t count = 0;
    while (*s) count += write(static_cast<uint8_t>(*s++));
    return count;
  }
  template<typename T> size_t print(T) { return write('0'); }
  template<typename T> size_t print(T value, int) { return print(value); }
  size_t println() { return write('\n'); }
  template<typename T> size_t println(T value) { return print(value) + println(); }
  template<typename T> size_t println(T value, int n) { return print(value, n) + println(); }
};
struct FakeSerial : Print {
  std::string text;
  size_t write(uint8_t b) override { text += static_cast<char>(b); return 1; }
} Serial;
struct Sd2Card {
  bool init(int, int) { ++io.mounts; return io.failMountStage != 1; }
  uint8_t errorCode() { return io.cardError; }
};
struct SdVolume {
  bool init(Sd2Card *) { ++io.mounts; return io.failMountStage != 2; }
};
class SdFile : public Print {
  std::string activeName;
  bool readOnly = false;
public:
  size_t write(uint8_t b) override {
    ++io.writes;
    if (readOnly || (io.failWriteAt > 0 && io.writes >= io.failWriteAt)) return 0;
    if (!activeName.empty()) io.files.at(activeName) += static_cast<char>(b);
    return 1;
  }
  bool sync() { ++io.syncs; return !io.failSync; }
  bool close() {
    ++io.closes;
    if (!sync() || io.failClose) return false;
    activeName.clear(); readOnly = false;
    return true;
  }
  bool open(SdFile *, const char *name, uint8_t mode) {
    const bool exists = io.files.count(name) != 0;
    if (mode == O_READ) {
      ++io.probes;
      if (io.failProbeIo) { io.cardError = 1; return false; }
      if (io.ambiguousProbe == name || !exists) return false;
      activeName = name; readOnly = true;
      return true;
    }
    ++io.createAttempts; io.lastCreateMode = mode;
    if (io.failCreate || ((mode & O_EXCL) && exists)) return false;
    if (!exists && !(mode & O_CREAT)) return false;
    if (!exists || (mode & O_TRUNC)) io.files[name] = "";
    activeName = name; readOnly = !(mode & O_WRITE);
    return true;
  }
  bool openRoot(SdVolume *) { ++io.mounts; return io.failMountStage != 3; }
};
constexpr int SND_ERROR = 1, SND_SCAN_STOP = 2, SND_CLICK = 3;
std::vector<int> sounds;
void soundStart(int sound) { sounds.push_back(sound); }
void soundPlayBlocking(int sound) { sounds.push_back(sound); }
int recoveryConversions = 0;
bool adsDiscardConversions(uint8_t count) {
  recoveryConversions += count; nowMs += count * 100; return true;
}
void printAlarmRule(float) {}
int calibrations = 0;
void actionSystemZeroCalibrate() { ++calibrations; }
void printMv(Print &out, float value) { out.print(value); }
void actionStopScan();
'''

STATE = r'''
Phase phase = READY;
Button btnStop = {PIN_BTN_STOP, true, true, 0};
bool stopHoldArmed = false;
uint32_t stopPressedMs = 0;
bool baselineReady = true, alarmActive = false;
bool sysocalDone = true, adcOk = true, inputFault = false;
bool sdReady = true, sdFault = false, sdNeedsRestart = false, logOpen = true;
uint8_t scanStatus = SCAN_RUNNING, abnormalRun = 0;
int8_t abnormalSide = 0;
uint16_t nextScanIndex = 0;
char logFileName[13] = "SCAN0001.CSV";
uint32_t phaseStartMs = 0, sampleNumber = 100, lastFlushMs = 0;
uint32_t droppedPoints = 0, saturatedPoints = 0;
uint32_t settlingPoints = 0, learningPoints = 0, scanNormal = 0, scanAbnormal = 0;
uint32_t alarmEvents = 0, alarmPoints = 0, maxDeltaPoint = 0;
uint32_t firstAlarmPoint = 0, lastAlarmPoint = 0;
float initialBaselineMv = 1, baselineMv = 1, maxAbsDelta = 0, maxDeltaPct = 0;
bool maxDeltaPctValid = false;
Stats baseStats = {}, scanStats = {};
float lastReadingMv = 0, scanPercent = 10;
uint32_t lastIdlePrintMs = 0, lastConversionMs = 0, alarmStartedMs = 0;
bool drdyArmed = false, baselineHeld = false;
float bSum[BASELINE_BUCKETS] = {};
uint16_t bCnt[BASELINE_BUCKETS] = {};
uint8_t bIdx = 0;
uint32_t bStartMs = 0, winCount = 0;
struct Row { uint32_t number, time; float reading, baseline; uint8_t phase, run; bool abnormal, alarm; };
std::vector<Row> rows;
void logScanRow(uint32_t number, uint32_t time, float reading, float baseline,
                float, uint8_t phase, bool abnormal, uint8_t run, bool alarm) {
  rows.push_back({number, time, reading, baseline, phase, run, abnormal, alarm});
}
Sd2Card sdCard;
SdVolume sdVolume;
SdFile sdRoot;
CheckedLogFile logFile;
void lbl(uint8_t) { logFile.print("Label,"); }
void printSeconds(uint32_t) { logFile.print("0.000"); }
'''

TESTS = r'''
int checks = 0;
void check(bool result, const char *message) {
  ++checks;
  if (!result) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void reset(Phase nextPhase = READY) {
  nowMs = 0;
  std::fill(std::begin(pins), std::end(pins), HIGH);
  btnStop = {PIN_BTN_STOP, true, true, 0};
  stopHoldArmed = false; stopPressedMs = 0; calibrations = 0;
  phase = nextPhase; baselineReady = true; alarmActive = false;
  sysocalDone = true; adcOk = true; inputFault = false;
  sdReady = true; sdFault = false; sdNeedsRestart = false; logOpen = true;
  scanStatus = SCAN_RUNNING; abnormalRun = 0; abnormalSide = 0;
  std::strcpy(logFileName, "SCAN0001.CSV");
  logFile = CheckedLogFile{}; io = {}; sounds.clear(); Serial.text.clear();
  nextScanIndex = 0;
  baseStats = {}; scanStats = {};
  sampleNumber = 0; phaseStartMs = 0; lastIdlePrintMs = 0;
  settlingPoints = learningPoints = scanNormal = scanAbnormal = 0;
  alarmEvents = alarmPoints = firstAlarmPoint = lastAlarmPoint = 0;
  maxAbsDelta = maxDeltaPct = 0; maxDeltaPctValid = false;
  recoveryConversions = 0; rows.clear(); scanPercent = 10;
  baselineReset();
  baselineReady = nextPhase != SETTLING && nextPhase != LEARNING;
}
void poll(uint32_t time, int level) {
  nowMs = time; pins[PIN_BTN_STOP] = level; pollStopButton();
}
void press(uint32_t time) {
  poll(time, LOW); poll(time + DEBOUNCE_MS + 1, LOW);
}
int soundCount(int sound) { return std::count(sounds.begin(), sounds.end(), sound); }
void expectFailedSave(const char *message) {
  check(sdFault && sdNeedsRestart && !sdReady && !logOpen, message);
  check(scanStatus == SCAN_SD_FAULT, "Failed storage must void scan status");
  check(soundCount(SND_SCAN_STOP) == 0, "Failed save must never play saved cue");
  check(Serial.text.find("SAVED. READY.") == std::string::npos,
        "Failed save must never claim saved on serial");
  check(soundCount(SND_ERROR) > 0, "Failed save needs fault cue");
}
void testStopButton() {
  reset();
  press(100); poll(500, HIGH); poll(540, HIGH); poll(5000, HIGH);
  check(calibrations == 0, "Short idle STOP must not recalibrate");
  check(!stopHoldArmed, "Released idle STOP must cancel hold");
  actionStopScan();
  check(calibrations == 0, "Serial/ordinary STOP while idle must do nothing");

  reset();
  press(100);
  const uint32_t acceptedAt = 100 + DEBOUNCE_MS + 1;
  poll(acceptedAt + IDLE_ZERO_HOLD_MS - 1, LOW);
  check(calibrations == 0, "Idle calibration must wait full hold duration");
  poll(acceptedAt + IDLE_ZERO_HOLD_MS, LOW);
  check(calibrations == 1, "Two-second idle STOP should recalibrate");
  poll(10000, LOW);
  check(calibrations == 1, "Held STOP must recalibrate only once");
  poll(10100, HIGH); poll(10140, HIGH); press(10200); poll(13000, LOW);
  check(calibrations == 2, "A release permits another deliberate hold");

  reset(); press(100);
  poll(acceptedAt + IDLE_ZERO_HOLD_MS, HIGH);
  check(calibrations == 0, "Raw release at hold boundary must suppress calibration");
  poll(acceptedAt + IDLE_ZERO_HOLD_MS + 40, HIGH);
  check(!stopHoldArmed, "Debounced release cancels pending boundary hold");

  reset(); poll(100, LOW); poll(110, HIGH); poll(120, LOW); poll(140, HIGH);
  poll(4000, HIGH);
  check(calibrations == 0 && !stopHoldArmed, "Button bounce must not arm calibration");

  for (Phase active : {SETTLING, LEARNING, SCANNING}) {
    reset(active); press(100);
    check(phase == READY && !logOpen, "Active STOP must immediately end/save scan after debounce");
    poll(8000, LOW);
    check(calibrations == 0 && !stopHoldArmed,
          "Holding STOP after ending a scan must never trigger calibration");
  }
  reset(); press(100); phase = SCANNING; poll(500, LOW);
  phase = READY; poll(5000, LOW);
  check(calibrations == 0, "Starting a scan cancels a pending idle calibration hold");

  reset();
  const uint32_t wrapStart = UINT32_MAX - 500;
  press(wrapStart);
  const uint32_t wrapAccepted = wrapStart + DEBOUNCE_MS + 1;
  poll(wrapAccepted + IDLE_ZERO_HOLD_MS - 1, LOW);
  check(calibrations == 0, "Hold must not fire early over millis rollover");
  poll(wrapAccepted + IDLE_ZERO_HOLD_MS, LOW);
  check(calibrations == 1, "Hold must work across 32-bit millis rollover");
  std::cout << "PASS: STOP debounce, long hold, cancellation, scan stop, rollover\n";
}
void testIndicators() {
  reset();
  for (uint32_t t : {0U, 99U, 100U, 999U, 1000U, 1999U}) {
    nowMs = t; updateLeds();
    check(pins[PIN_LED_GREEN] == HIGH, "READY must stay steady green");
  }
  phase = SCANNING;
  int dark = 0;
  for (nowMs = 0; nowMs < 2000; ++nowMs) {
    updateLeds(); dark += pins[PIN_LED_GREEN] == LOW;
  }
  check(dark == 200, "RECORDING must wink off for 100 ms per second");
  for (Phase busy : {SETTLING, LEARNING}) {
    phase = busy; nowMs = 50; updateLeds();
    check(pins[PIN_LED_GREEN] == HIGH, "Busy green blink on phase");
    nowMs = 150; updateLeds();
    check(pins[PIN_LED_GREEN] == LOW, "Busy green blink off phase");
  }
  phase = READY; sdFault = true; nowMs = 0; updateLeds();
  check(pins[PIN_LED_RED] == HIGH, "Storage fault must wink red");
  nowMs = 100; updateLeds();
  check(pins[PIN_LED_RED] == LOW, "Fault wink must be distinguishable from steady alarm");
  alarmActive = true; updateLeds();
  check(pins[PIN_LED_RED] == HIGH, "Active alarm retains steady red priority");
  alarmActive = false; sysocalDone = false; nowMs = 500; updateLeds();
  check(pins[PIN_LED_GREEN] == LOW, "Missing calibration keeps its slow green warning");
  std::cout << "PASS: ready/recording/busy LEDs and existing fault/alarm priorities\n";
}
void testStorage() {
  reset(SCANNING);
  check(logFlushChecked() && phase == SCANNING && logOpen,
        "Successful periodic sync must preserve the active scan");
  check(io.syncs == 1 && io.closes == 0 && sounds.empty(),
        "Periodic sync must not close file or play saved cue");

  reset(SCANNING); actionStopScan();
  check(phase == READY && !logOpen && !sdFault, "Successful STOP must finish saving");
  check(scanStatus == SCAN_COMPLETE, "STOP after active scanning must report COMPLETE");
  check(io.closes == 1 && io.syncs == 1, "Final save must check close/sync once");
  check(soundCount(SND_SCAN_STOP) == 1, "Successful STOP plays one saved cue");
  check(Serial.text.find("SAVED. READY.") != std::string::npos, "Confirmed save is reported");

  reset(LEARNING); baselineReady = false; actionStopScan();
  check(scanStatus == SCAN_EARLY_STOP && soundCount(SND_SCAN_STOP) == 1,
        "Saved early stop is reported as incomplete, with saved cue");

  reset(SCANNING); io.failWriteAt = 1; actionStopScan();
  expectFailedSave("Byte write failure must latch storage fault");
  check(io.writes == 1 && io.syncs == 0 && io.closes == 0,
        "No further card write/sync/close after first failed byte");
  check(logFile.write('X') == 0 && io.writes == 1, "Latched byte failure rejects later writes");

  reset(SCANNING); io.failSync = true; actionStopScan();
  expectFailedSave("Final sync failure must latch storage fault");
  check(io.closes == 1 && io.syncs == 1, "Failed close sync must not be retried");

  reset(SCANNING); io.failClose = true; actionStopScan();
  expectFailedSave("Final close failure must latch storage fault");
  check(io.closes == 1, "Failed final close must not be retried");

  reset(SCANNING); io.failSync = true;
  check(!logFlushChecked(), "Periodic sync failure must return false");
  expectFailedSave("Periodic sync failure must stop scan and latch storage fault");
  check(phase == READY && io.syncs == 1 && io.closes == 0,
        "Periodic failure must stop scan without closing/retrying dirty handle");
  const int failedSyncCalls = io.syncs;
  check(!logFlushChecked() && !logClose() && !sdStart(), "All retries remain blocked until restart");
  check(io.syncs == failedSyncCalls && io.closes == 0 && io.mounts == 0,
        "Blocked storage retries must perform no card I/O");

  reset(SCANNING); logOpen = false; actionStopScan();
  check(soundCount(SND_SCAN_STOP) == 0, "Missing file cannot earn a saved cue");
  for (int stage : {1, 2, 3}) {
    reset(); sdReady = false; logOpen = false; io.failMountStage = stage;
    check(!sdStart() && sdNeedsRestart && sdFault, "Mount failure must latch restart requirement");
    const int calls = io.mounts; io.failMountStage = 0;
    check(!sdStart() && io.mounts == calls, "Mount failure must block same-boot remount");
  }
  std::cout << "PASS: successful save and byte/sync/close/mount failure handling\n";
}
void testNewFiles() {
  reset(); logOpen = false;
  io.files = {{"SCAN0001.CSV", "first existing data"},
              {"SCAN0002.CSV", "second existing data"}};
  const auto existing = io.files;
  check(logOpenNew("SCAN", nextScanIndex), "First available scan file should open");
  check(std::string(logFileName) == "SCAN0003.CSV" && nextScanIndex == 4,
        "Filename search must skip occupied names");
  check(io.probes == 3 && io.closes == 2 && io.createAttempts == 1,
        "Only occupied probes are closed and first missing file is created");
  check(io.lastCreateMode == (O_CREAT | O_EXCL | O_WRITE),
        "Scan creation must request exclusive write creation without append/truncate");
  for (const auto &file : existing)
    check(io.files.at(file.first) == file.second, "Probing existing files must preserve their content");
  check(io.files.at("SCAN0003.CSV").empty(), "New scan file starts empty");
  check(logFile.write('N') == 1 && io.files.at("SCAN0003.CSV") == "N",
        "Subsequent scan bytes must reach only the newly created file");

  reset(); logOpen = false;
  io.files = {{"SCAN0001.CSV", "do not overwrite"}};
  io.ambiguousProbe = "SCAN0001.CSV";
  check(!logOpenNew("SCAN", nextScanIndex), "Ambiguous missing probe must not reopen an existing file");
  check(io.createAttempts == 1 && io.files.at("SCAN0001.CSV") == "do not overwrite",
        "Exclusive creation must preserve an existing file even after misleading probe");
  expectFailedSave("Ambiguous probe/create collision must latch storage fault");

  reset(); logOpen = false; io.failProbeIo = true;
  check(!logOpenNew("SCAN", nextScanIndex), "Probe I/O failure must abort file selection");
  expectFailedSave("Probe I/O failure must require restart");
  check(io.probes == 1 && io.createAttempts == 0 && io.closes == 0,
        "Probe I/O failure must not create, overwrite, or retry close");

  reset(); logOpen = false;
  io.files = {{"SCAN0001.CSV", "existing data"}}; io.failClose = true;
  check(!logOpenNew("SCAN", nextScanIndex), "Failed probe close must abort file selection");
  expectFailedSave("Failed probe close must require restart");
  check(io.closes == 1 && io.createAttempts == 0 && io.files.at("SCAN0001.CSV") == "existing data",
        "Probe-close failure must stop without creating or modifying a file");

  reset(); logOpen = false; io.failCreate = true;
  check(!logOpenNew("SCAN", nextScanIndex), "Failed exclusive create must return failure");
  expectFailedSave("Failed creation must require restart");
  check(io.files.empty() && io.createAttempts == 1 && nextScanIndex == 1,
        "Failed creation cannot leave a reported-open file or consume another filename");

  reset(); logOpen = false; nextScanIndex = MAX_FILE_INDEX;
  io.files = {{"SCAN9999.CSV", "last existing data"}};
  check(!logOpenNew("SCAN", nextScanIndex), "Exhausted filename space must refuse new scan");
  check(!logOpen && sdFault && io.createAttempts == 0,
        "Filename exhaustion must report failure without creating a file");
  check(io.files.at("SCAN9999.CSV") == "last existing data" && io.files.size() == 1,
        "Filename exhaustion must preserve the last existing file");
  check(soundCount(SND_SCAN_STOP) == 0, "Filename exhaustion cannot play saved cue");
  std::cout << "PASS: exclusive scan creation, occupied names, probe faults, exhaustion\n";
}
void testAcquisitionWorkflow() {
  reset(SETTLING);
  for (uint32_t time = 100; time <= 1000; time += 100) {
    nowMs = time; handlePoint(0.020f);
  }
  check(phase == LEARNING && settlingPoints == 10 && learningPoints == 0,
        "First second is settling and does not learn a baseline");
  check(winCount == 0 && scanStats.n == 0 && !alarmActive,
        "Settling samples must not enter the reference or active statistics");
  for (uint32_t time = 1100; time <= 6000; time += 100) {
    nowMs = time; handlePoint(0.020f);
  }
  check(learningPoints == 50 && baseStats.n == 50 && baselineReady,
        "Five seconds of learning must establish a baseline");
  check(soundCount(SND_CLICK) == 1 && recoveryConversions == 3,
        "Learning ends with one short beep and three recovery conversions");
  check(phase == SCANNING && nowMs == 6300 && lastConversionMs == 6300,
        "Active scanning starts only after recovery conversions finish");
  check(rows.size() == 60 && rows.back().phase == PH_LEARN,
        "Recovery conversions must not become active-scan CSV rows");
  nowMs = 6400; handlePoint(0.020f);
  check(rows.back().phase == PH_SCAN && rows.back().number == 61 && rows.back().time == 6400,
        "First active reading must preserve elapsed time across recovery gap");
  check(!alarmActive && scanNormal == 1, "First normal active point remains quiet");

  reset(SCANNING); baselineAccept(0.020f);
  nowMs = 100; handlePoint(0.024f);
  check(abnormalRun == 1 && !alarmActive && winCount == 1,
        "First abnormal point is logged but excluded from the baseline");
  nowMs = 200; handlePoint(0.016f);
  check(abnormalRun == 1 && !alarmActive && abnormalSide == -1,
        "High-low disturbance must not confirm an alarm");
  nowMs = 300; handlePoint(0.015f);
  check(abnormalRun == 2 && alarmActive && alarmEvents == 1,
        "Second low point confirms a same-side alarm");
  nowMs = 400; handlePoint(0.024f);
  check(abnormalRun == 1 && !alarmActive && abnormalSide == 1,
        "Opposite abnormal side clears active alarm and restarts confirmation");
  nowMs = 500; handlePoint(0.025f);
  check(alarmActive && alarmEvents == 2, "Second high point starts a new alarm event");
  nowMs = 600; handlePoint(0.020f);
  check(abnormalRun == 0 && abnormalSide == 0 && !alarmActive && winCount == 2,
        "Normal point clears confirmation and updates the reference");

  reset(); baselineAccept(0.020f);
  nowMs = 1000; baselineAgeOut(nowMs); baselineAccept(0.022f);
  nowMs = 30000; baselineAgeOut(nowMs);
  check(winCount == 1 && std::fabs(baselineMv - 0.022f) < 0.000001f,
        "Baseline must expire old time buckets even without newly accepted points");
  nowMs = 31000; baselineAgeOut(nowMs);
  check(winCount == 0 && baselineHeld && std::fabs(baselineMv - 0.022f) < 0.000001f,
        "An empty time window holds the last baseline");
  std::cout << "PASS: settling/learning/recovery timing, same-side alarms, time-aged baseline\n";
}
int main() {
  static_assert(sizeof(uint32_t) == 4, "AVR millis requires 32-bit arithmetic");
  testStopButton(); testIndicators(); testStorage(); testNewFiles(); testAcquisitionWorkflow();
  std::cout << "PASS: " << checks << " behavior assertions against extracted firmware\n";
}
'''


def harness(source: str) -> str:
    constants = ("PIN_BTN_STOP", "PIN_LED_GREEN", "PIN_LED_RED", "PIN_SD_CS",
                 "DEBOUNCE_MS", "IDLE_ZERO_HOLD_MS", "BLINK_GREEN_WHEN_BUSY",
                 "FAULT_WINK_MS", "FAULT_WINK_PERIOD_MS", "REQUIRE_SD_FOR_SCAN",
                 "MV_DECIMALS", "SCAN_RUNNING", "MAX_FILE_INDEX",
                 "SETTLE_MS", "INITIAL_LEARNING_MS", "BASELINE_WINDOW_MS",
                 "BASELINE_BUCKET_MS", "BASELINE_BUCKETS", "IDLE_PRINT_MS",
                 "ALARM_CONFIRM_POINTS", "ALARM_BOTH_DIRECTIONS", "MIN_THRESHOLD_MV",
                 "PH_SETTLE")
    definitions = [constant(source, name) for name in constants]
    for kind, name in (("struct", "Button"), ("struct", "Stats"), ("enum", "Phase"),
                       ("class", "CheckedLogFile")):
        definitions.append(block(source, rf"^{kind}\s+{name}\b[^{{]*{{", semicolon=True))
    functions = ("buttonWasPressed", "recording", "zeroCalMissing", "faultActive",
                 "alarmLimits", "baselineRecompute", "baselineReset", "baselineAgeOut",
                 "baselineAccept", "statsAdd",
                 "statsSpread", "sdStart", "sdWriteFailed", "logFlushChecked",
                 "makeFileName", "logOpenNew",
                 "logClose", "updateLeds", "scanStatusText", "finishScanFile",
                 "actionStopScan", "pollStopButton", "handlePoint")
    code = "\n\n".join([MOCKS, *definitions, STATE,
                           *(function(source, name) for name in functions), TESTS])
    # AVR unsigned long is 32 bits. Preserve that arithmetic on any host ABI,
    # especially in debounce/hold subtraction across millis() wraparound.
    return re.sub(r"\bunsigned\s+long\b", "uint32_t", code)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="Path to a C++17 compiler")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("g++")
    if not compiler:
        bundled = Path(r"C:\msys64\ucrt64\bin\g++.exe")
        compiler = str(bundled) if bundled.is_file() else None
    if not compiler:
        raise SystemExit("C++ compiler unavailable; pass --compiler with the g++ path")
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="millivolt-reliability-") as work:
        cpp = Path(work) / "reliability.cpp"
        executable = Path(work) / ("reliability.exe" if os.name == "nt" else "reliability")
        cpp.write_text(harness(INO.read_text(encoding="utf-8")), encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
                        str(cpp), "-o", str(executable)], check=True, env=env)
        subprocess.run([str(executable)], check=True, env=env)


if __name__ == "__main__":
    main()
