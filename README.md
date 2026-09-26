# Microvolt ADC

Arduino Uno R3 firmware for a millivolt measurement and alarm prototype. An ADS1256 digitizes the antenna input; the sketch learns a local baseline, signals threshold excursions with LEDs and a buzzer, and writes scan results to a microSD card.

The current sketch is [firmware/Millivolt_Monitor/Millivolt_Monitor.ino](firmware/Millivolt_Monitor/Millivolt_Monitor.ino) (v3.3). Earlier versions are available through the Git history for comparison. They are historical snapshots, not release candidates.

## Firmware progression

| Snapshot | Main change |
| --- | --- |
| v1.1 | Initial Uno, ADS1256, alarm and logging prototype |
| v1.2 | Confirmed ±10% alarm rule and separate baseline/scan CSV logs |
| v2.7 | Later acquisition and logging implementation |
| v3.0 | ADC and SD fault handling, startup calibration checks and scan outcomes |
| v3.1 | Button controlled 10%, 50% and 100% alarm thresholds |
| v3.2 | Shorter, lower pitched feedback tones |
| v3.3 | Checked SD writes and close, fault latching, scan/ready LED patterns and STOP handling |

These commits **import existing source snapshots**. Their Git dates show the import date, not when the original development occurred. The version names identify the saved firmware revisions. This public repository excludes client originals, correspondence, and field measurements.

## Build and checks

The controlled build uses Arduino AVR Boards 1.8.8, AVR GCC 7.3.0-atmel3.6.1-arduino7, and Arduino SD 1.3.0. On Windows with those packages installed in the standard Arduino15 location:

```powershell
.\tools\build_firmware.ps1 -Configuration All
```

The script writes Production and TestMode HEX files under `firmware/builds/`. TestMode is only for bench testing. The build on 25 September 2026 used 29,832 of 32,256 flash bytes and 1,365 of 2,048 SRAM bytes for Production.

The host harness compiles functions extracted from the current sketch with mocked hardware and SD interfaces. It passed 133 behavior assertions on 25 September 2026:

```powershell
python tools/test_firmware_reliability.py
```

Those checks cover button timing, LED states, save failures, exclusive log creation, calibration/learning timing, alarm confirmation and baseline aging. They do **not** establish analog accuracy, SD card behavior in the assembled unit, or dependable walking performance. Integrated hardware acceptance remains to be completed.
