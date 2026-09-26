/*
  =====================================================================
   MILLIVOLT MEASUREMENT AND ALARM SYSTEM                      v3.2
   Arduino UNO R3  +  ADS1256 24-bit ADC  +  microSD logging
  =====================================================================

   What this does (plain English)
   ------------------------------
   The loop-stick antenna produces a tiny DC voltage (thousandths of a
   volt = millivolts, and the interesting part of the client's range is
   thousandths of THAT).  This sketch reads it with a 24-bit converter
   and runs the workflow the client asked for:

     Power on
       ->  the unit calibrates its own electrical zero, by itself.  A
           relay shorts the antenna connector, the ADS1256 learns that
           condition as zero (SYSOCAL), the relay releases.  Green LED
           blinks throughout and goes solid when it is done.
     READY
       ->  the operator puts the antenna in its normal scanning
           position over ordinary ground.
     START
       ->  a short click immediately confirms that the button was seen.
           If the power-on zero was not accepted, START safely retries it
           once and continues automatically when it succeeds.
       ->  the FIRST 5 SECONDS of the real scan are the ground
           baseline.  The alarm is held off while it is learned.
       ->  after that every reading is compared with the CURRENT
           baseline:
             inside  baseline +/- selected %  = NORMAL, and the reading is
               allowed into the rolling 30-second baseline average
             outside                    = ABNORMAL, logged, counted,
               and NEVER allowed into the baseline
           2 abnormal readings in a row  ->  red LED + buzzer.
     STOP
       ->  the CSV file is closed and a summary is printed.

   The baseline therefore follows ordinary ground as it changes while
   the operator walks, but an anomaly can never teach the unit that the
   anomaly is normal.

   TWO DIFFERENT THINGS THAT ARE EASY TO CONFUSE
   ---------------------------------------------
   They happen at different times, for different reasons:

     relay + SYSOCAL, at power-up   =  ELECTRICAL ZERO calibration.
         Removes the offset of the converter, the 1 k front-end
         resistors, the bias network and the wiring.  Nothing to do
         with soil.  The antenna can be anywhere.

     first 5 s after START          =  GROUND BASELINE learning.
         The normal millivolt level of the earth being surveyed.
         The antenna MUST be in its normal scanning position over
         representative ordinary ground - NOT held in the air.

   Alarm rule:
     alarm when  reading > baseline + P %   or   reading < baseline - P %
     P is selected by the D2 button: 10 %, 50 %, or 100 %.  Each press
     while idle advances one setting and confirms it with one, two, or
     three short beeps.  START freezes the selection for that scan and
     writes it into the CSV header as Alarm_Threshold_Percent.
     A smaller percentage is more sensitive; a larger percentage requires
     a larger change from the baseline before the alarm sounds.
     Power-on defaults to 10 %.  Button presses during a scan are ignored.

   Logging:
     Every reading goes to the Serial Monitor (115200 baud).  With a
     microSD card fitted, EVERY SCAN gets its own file - SCAN0001.CSV,
     SCAN0002.CSV, ...  Each file starts with a header block (file ID,
     alarm %, learning time, window length, and whether the automatic
     zero calibration was valid), then one row per point, then a
     summary.  In Excel: select the reading / baseline / limit columns
     and insert a line chart -> the same graph as the client's own
     spreadsheet.
     There are no BASExxxx.CSV files any more.  There is no separate
     baseline recording to put in one.

   You can also drive the unit from the Serial Monitor: 1/3 = START,
   2/4 = STOP, s = status, v = version, r = re-check the ADC,
   c = automatic zero calibration, ? = help.

   Sounds
   ------
     power-on ........ three rising notes
     threshold button  one beep = 10 %, two = 50 %, three = 100 %
     START received .. one short click; wait while preflight finishes
     zero cal done ... two beeps, same pitch
     scan started .... one long beep
     baseline ready .. two rising notes - the alarm is now live
     scan stopped .... two falling notes
     ALARM ........... beep-beep-beep while the reading is outside the limits
     fault ........... fast beeps, red LED winks

   LEDs
   ----
     green blinking (fast, ~3 Hz) .. calibrating the electrical zero
     green blinking (very fast) .... settling, then learning the baseline
     green blinking (slow wink) .... READY but NOT zero-calibrated;
                                     START will retry once
     green solid ................... ready, or scanning normally
     red solid ..................... ALARM
     red winks once every 2 s ...... a fault: ADC, SD card, or a missing
                                     zero calibration (details on the
                                     Serial Monitor and in 's')

   ---------------------------------------------------------------------
   What changed in v3.0  -  RELEASE HARDENING AND CLEAN CSV
   ---------------------------------------------------------------------
   * ADC silence now voids an active scan immediately.  It can no longer
     be reported COMPLETE if STOP is pressed before the recovery retry.
   * Consecutive full-scale conversions are treated as an input fault,
     excluded from the baseline, and void an active scan.
   * SD-card mount, file-create and write failures are visible faults.
     REQUIRE_SD_FOR_SCAN defaults to true, so a field scan cannot appear
     healthy while saving nothing.
   * Zero calibration now has to leave a measured residual within
     CAL_VERIFY_MV before START is allowed.
   * STOP before baseline learning finishes is marked INCOMPLETE, not
     COMPLETE.  Serial and CSV status messages now agree on every exit.
   * CSV fields are no longer padded with spaces.  Empty values are truly
     empty, so strict parsers and exact-match filters work without TRIM().
   * The knob deadband was reduced so a slow end-to-end turn reaches all
     181 half-percent settings from 10.0 % through 100.0 %.
   * TEST_MODE identifies simulated data in the CSV and provides 20 %,
     60 % and 120 % anomaly levels for endpoint verification.
   * START now acknowledges the button immediately and, when necessary,
     retries zero calibration once before either starting automatically or
     reporting a clear four-beep/red-wink fault.

   ---------------------------------------------------------------------
   What changed in v2.9  -  KNOB RANGE 10 % TO 100 %
  ---------------------------------------------------------------------
   The active A0 threshold knob now covers 10.0 % through 100.0 %, still
   in 0.5 % steps.  Fully toward the low end is the most sensitive setting:
   a 10 % change from the rolling baseline can alarm.  At the high end a
   reading must move by 100 % of the baseline before it can alarm.

   The setting is still frozen when START is pressed, printed on Serial,
   and saved as Alarm_Threshold_Percent in that scan's CSV.  The detection,
   rolling-baseline, confirmation, calibration and logging paths are
   otherwise unchanged from v2.8.

  ---------------------------------------------------------------------
   What changed in v2.8  -  THE ALARM PERCENTAGE IS ON A KNOB
  ---------------------------------------------------------------------
   The 10 k potentiometer on A0, wired since v1.x and ignored since the
   client fixed the rule at 10 %, now sets the alarm percentage: 5.0 %
   to 30.0 % in 0.5 % steps (KNOB_MIN_PERCENT / KNOB_MAX_PERCENT).

     * FROZEN AT START.  The knob is read once when START is pressed and
       that value is used for the whole scan.  Turning it mid-scan cannot
       change a recording, and a file never contains two rules.  During a
       scan the knob is not read at all, so the scan path is unchanged.

     * THE FILE SAYS WHICH.  Alarm_Threshold_Percent in the CSV header
       was already derived from the rule in force, so it now carries the
       knob's value with no change to the file format.

     * VISIBLE ON THE BENCH.  While idle, turning the knob prints the new
       setting (a slow turn prints every 0.5 % step).  START prints the
       frozen value, as do the banner and 's'.  In the field, with no
       Serial Monitor, the knob needs a marked scale: at 5 - 30 % the
       centre is 17.5 % and 10 % is about one fifth of the way round.

     * NO FLICKER.  The reading is an average of 16 and has a 6-count
       deadband, so a knob parked between two steps cannot flip between
       them - and the value START freezes is the value last printed.

   Built from a first draft of this feature that was written against an
   older copy (v2.4 plus the v2.5 change) and called itself v2.6;
   dropped in as it was, it would have discarded the real v2.6 (the
   aligned CSV) and v2.7 (the armed tone).  That draft also did not compile: static_assert cannot
   test a plain "const float".  Here the limits are constexpr and the
   compiler also checks they are whole or half percentages.

   Also fixed while here:
     * 's' during the settle and learning period showed the knob's live
       value rather than the rule the scan had frozen.
     * Alarm limits are printed only while a scan is running.  After STOP
       they were being quoted against the previous scan's baseline.
     * A comment in automaticZeroCalibration() still promised that the
       learning period aborts a near-zero baseline.  v2.5 removed that
       abort; the comment now says so.  (Comment only.)

   Paid for, at 99 % flash, by merging the three-line start-up banner
   into one and shortening three rare Serial messages ("not scanning",
   "ADC not responding - scan refused", "file numbers all used").
   Nothing else in the measurement, detection or file format changed.
   With KNOB_ENABLED 0 this build is 150 bytes SMALLER than v2.7.
   32,088 of 32,256 bytes (99 %), 168 free, on arduino:avr 1.8.8.

  ---------------------------------------------------------------------
   What changed in v2.7  -  A TONE WHEN THE ALARM GOES LIVE
  ---------------------------------------------------------------------
   Two rising notes at the end of the baseline-learning period, the
   moment the instrument starts judging readings.

   Until now the operator got a tone at START and then silence until
   something alarmed, with no way to tell - while walking, without
   looking at the LED - whether the unit was still learning or already
   watching.  Anything that happened during those six seconds was missed
   in a way nothing announced.

   The tone is RISING, which distinguishes it by ear from the three that
   already exist: START is one flat note, zero calibration is two equal
   beeps and STOP falls.

   It sounds AFTER the learning window closes, so the buzzer cannot
   disturb the readings that built the baseline.  It does coincide with
   the first scanning readings; the bias network moves with the 5 V rail
   but does so on both inputs, so a differential measurement should
   reject it.  Worth a glance at the first few scanning rows on the
   bench to confirm that in practice.

   Note the timing: the tone lands about SIX seconds after START, not
   five - one second of settling plus five of learning.  Both are
   settings (SETTLE_MS, INITIAL_LEARNING_MS) if that should change.

  ---------------------------------------------------------------------
   What changed in v2.6  -  THE READINGS BLOCK LINES UP
  ---------------------------------------------------------------------
   Every field in the readings block is now padded to a constant width,
   so a row read in a text editor sits under its own heading.  Unpadded,
   a settling row was

       1,0.100,0.03708,,,,,,,SETTLING,NOT_EVALUATED,0,NO

   and finding which column you were in meant counting six consecutive
   commas by eye.  Now:

       Point,   Time_s,Reading_mV,Baseline_mV,Thresh_mV, Lower_mV, ...
           1,    0.100,   0.03708,           ,         ,         , ...
          61,    6.100,   0.02321,    0.02303,  0.00230,  0.02073, ...

     * Numbers are right-aligned, text left-aligned, and the last column
       is not padded so no line carries trailing whitespace.

     * The column names had to get shorter, because the heading sets the
       column width: "DifferenceFromBaseline_mV" is 25 characters to
       hold 9 characters of number, and carrying that across the whole
       row nearly tripled the file.  Short names cost a third of that
       and read the same next to their units.

     * printSeconds() now right-aligns the whole-seconds part.  Without
       that the Time column alone would still have been ragged - "0.100"
       and "600.000" are not the same width - which is the sort of thing
       that looks fixed until the scan runs past ten seconds.

   IT IS STILL A PLAIN CSV.  Leading spaces in front of a number are
   ignored by Excel and by every CSV parser, so the file charts exactly
   as before.  The padding on the two text columns does reach the cell,
   which only matters for an exact-match formula against them - TRIM().

   THE COST IS FILE SIZE.  A scanning row goes from about 77 characters
   to 133, so a 20-minute scan is about 1.6 MB instead of 0.95 MB and the
   card takes roughly 70 % more write traffic.  That is nothing for the
   card, but Dropped_Points at 10 SPS has never been measured on
   hardware, and this change makes that measurement more important, not
   less.  It is the first number to read in the stop summary.

   98 % flash, 336 bytes free.

  ---------------------------------------------------------------------
   What changed in v2.5  -  ALLOW VERY SMALL BASELINES
  ---------------------------------------------------------------------
   Removed the automatic "baseline far too small" scan-abort check.
   A scan now continues even when the learned baseline is below 0.005 mV.
   This is useful when the real antenna naturally produces a baseline
   near zero.  Electrical zero calibration and all normal alarm logic
   are unchanged.

  ---------------------------------------------------------------------
   What changed in v2.4  -  WHAT THE NUMBERS ACTUALLY DESCRIBE
  ---------------------------------------------------------------------
   Three of these fix numbers that were wrong, not presentation.

     * A SETTLING PERIOD after START.  The first bench scan opened
       0.03708, 0.03148, 0.02344, 0.00390 mV before settling near 0.023 -
       transients from the button and from the 250 ms START tone, not
       from the ground.  Those four readings were going straight into the
       baseline and into the file's min/max.  SETTLE_MS (1 s) now sits
       between START and the learning period.  The readings are still
       logged, as SETTLING, so nothing is hidden; they simply enter
       nothing.

     * SEPARATE STATISTICS.  min/max/spread used to be accumulated from
       the first reading after START, so they described the start-up
       transient rather than the ground.  Learning readings now go to
       baseStats and scanning readings to scanStats, and the summary
       reports Baseline_Learning_Spread_mV and Scan_Minimum/Maximum/
       Spread_mV separately.

     * SEPARATE COUNTERS.  normalPoints counted the learning readings as
       "normal" even though they were never judged: a 193-point file
       reported 193 normal points when about 51 of them were baseline
       learning.  Now Settling_Points, Baseline_Learning_Points,
       Scan_Normal_Points and Scan_Abnormal_Points, with Total_Points
       still covering everything.

     * PHASE IS NOT A CLASSIFICATION.  The old Classification column held
       LEARN, NORMAL or ABNORMAL - two different ideas in one column, and
       no way to say "never judged".  Now Phase (SETTLING /
       BASELINE_LEARNING / SCANNING) and Classification (NOT_EVALUATED /
       NORMAL / ABNORMAL).

     * Self-describing column names (ElapsedTime_s, MeasuredVoltage_mV,
       BaselineUsed_mV, DifferenceFromBaseline_percent ...), an explicit
       ThresholdChange_mV column - the client asked for the threshold
       value, and a pair of limits only implies it - Firmware_Build_Date
       named so it cannot be mistaken for the scan date, Initial_ and
       Final_Baseline_mV with the change between them, and an explicit
       Scan_Status on every file rather than a VOID line on failures
       only.

   A NOTE ON WHAT THIS COST.  The UNO is full.  The complete set of
   improvements proposed in review came to 35,156 bytes against 32,256
   available - 2,900 over - and even after dropping the least valuable
   parts it would not fit.  What is here was paid for by compressing the
   Serial diagnostics: printStatus() is now four dense lines rather than
   fifteen, and the start-up banner states the alarm rule rather than
   every setting, because every setting is now in the CSV header of every
   file.  It still reports every piece of live state.
   Left out for want of flash, in the order they should be restored if
   room is ever found: Zero_Before/After_Calibration_mV and
   Offset_Removed_mV; First/Last_Abnormal_Point and
   Highest_Consecutive_Abnormal; the "Meaning" third column in the header
   block.  Those three groups cost about 1,400 bytes together.
   98 % flash, 400 bytes free.

  ---------------------------------------------------------------------
   What changed in v2.3  -  THE CSV IS WRITTEN TO BE READ
  ---------------------------------------------------------------------
   Same measurements, same detection, same everything electrical.  What
   changed is the file the operator actually opens.

     * The file is now five labelled blocks - [SCAN], [ZERO CALIBRATION],
       [ALARM SETTINGS], [READINGS], [SUMMARY] - separated by blank
       lines, instead of one run of bare key,value pairs top and bottom.

     * Every heading carries its unit: "Reading (mV)", "Time (s)",
       "Threshold (% of baseline)".  Nothing has to be remembered and
       nothing has to be looked up.

     * Labels are words rather than field names.  "alarm_percent" is now
       "Threshold (% of baseline)"; "max_abs_delta_mV" is now "Largest
       gap from baseline (mV)"; "points" is "Total readings".

     * Time is in SECONDS with three decimals instead of milliseconds,
       printed with integer maths so it is exact.  A chart's x axis is
       now in the units the operator thinks in.

     * THE LIMIT COLUMNS ARE LEFT EMPTY DURING THE LEARNING PERIOD.
       They used to be written as 0.00000 for the first five seconds,
       because no limits exist yet.  Charting them dragged the y axis
       down to zero and squashed the entire scan into the top few per
       cent of the plot - on the very graph the client said he wanted to
       use.  Empty cells simply leave those two lines unstarted.

     * The summary opens with "Scan result", so an abandoned recording
       announces itself at the top of the block rather than as a bare
       VOID line at the very end of the file.

     * Block markers are [SQUARE BRACKETED].  A cell beginning with "="
       is a formula to Excel, so "=== READINGS ===" would have opened as
       an error rather than as a heading.

   Paid for by shortening the Serial diagnostics, which the file now
   duplicates in a better form: 97 % flash, 964 bytes free - slightly
   more headroom than v2.2 had.

  ---------------------------------------------------------------------
   What changed in v2.2  -  AUTOMATIC ZERO CALIBRATION
  ---------------------------------------------------------------------
   v1.4 to v2.1 needed a human: short the input by hand, power on or
   type c, wait, then remove the short.  That is gone.  A reed relay
   does it now, and the sketch drives the relay.

     * NEW PIN.  D3 drives the zero-calibration relay through a 1 k base
       resistor into a 2N2222, with a 13 k base pull-down and a 1N4005
       flyback diode across the coil.
           D3 LOW  = relay OPEN   = antenna connected  = measuring
           D3 HIGH = relay CLOSED = CENTER shorted to SHELL = zero
       D3 was a spare in v2.0/v2.1.  It is not spare any more.  D2 is.

     * The contacts sit across CENTER and SHELL - across the antenna
       connector - NOT directly across AIN0/AIN1.  That is deliberate:
       it reproduces the manual jumper the calibration was proven with,
       and it puts the 1 k front-end resistors and the wiring INSIDE the
       loop that SYSOCAL cancels, so more of the real system offset is
       removed than a short at the chip pins could reach.

     * D3 is driven LOW as the very first act of setup(), before the
       buzzer, before Serial, before anything.  Combined with the 13 k
       hardware pull-down, which holds the transistor off while the MCU
       pin floats during boot and reset, the instrument's resting state
       is "measuring", never "shorted".

     * THE RELAY IS RELEASED ON EVERY EXIT PATH.  automaticZeroCalibration()
       is the only function that closes it and has a single exit, so
       success, a failed sanity check, a SYSOCAL timeout and a lost DRDY
       all reach the same relayOpen().  This is the one failure that
       would be invisible in the field: a relay left closed reads a flat
       ~0 uV for ever, with a green LED, no error, and a whole survey of
       plausible-looking nothing.

     * Five conversions are discarded after every relay movement.  The
       contacts settle in about a millisecond, but the 10 nF across the
       inputs and the ADS1256's digital filter take several conversion
       periods to follow the step.  0.5 s at 10 SPS, which is what the
       successful 21 Sep bench test used.

     * The sanity check is kept and is now MORE useful, not less.  If the
       relay fails to close - stuck contact, open coil, broken wire, dead
       transistor - the "shorted" reading is a real antenna signal, and
       SYSOCAL would cancel the very thing being measured.  Anything
       bigger than CAL_SANITY_MV (10 uV) aborts the calibration and says
       so.  The measured relay-closed zero on the bench was -2.53 uV, so
       a healthy relay passes with a factor of four to spare.

     * START now requires a VALID zero calibration, not merely one that
       has not been lost.  v2.1 tested sysocalLost, which only catches a
       calibration that existed and was then wiped; a start-up
       calibration that never succeeded left sysocalDone false AND
       sysocalLost false, and v2.1 would have allowed the scan.  v2.2
       tests sysocalDone.

     * An ADC re-initialisation during a scan VOIDS that scan.  Any
       reset or SELFCAL rewrites the offset registers, so the zero
       changes under a baseline that was learned with the old one.  The
       scan is stopped, the CSV is marked VOID in its own footer, the
       ADC is re-initialised, a fresh automatic zero calibration runs,
       and the unit returns to READY needing a new START and a new
       baseline.  It does not silently recalibrate and carry on, which
       would leave one file containing two different instruments.

     * Status reports ADC health and zero-calibration health separately,
       plus the relay state.  The converter can be perfectly healthy
       while the instrument is unfit to scan.

     * The A3 "ADS1256 RESET" pin is gone.  This module has no reset pin.
       The sketch uses the software RESET command (0xFE), which is what
       it was really relying on all along.

     * No ground baseline is learned at power-up.  baselineReady stays
       false until START, so the operator positions the antenna over
       representative ground first.

   Bench proof of the mechanism, 21 Sep 2026
   (RadioShack 275-0232 / OMR-C-105H reed relay, coil measured 258.2 ohm,
    KORAD supply into a 151 k / 99.7 ohm divider as the test signal):

       relay closed, zero BEFORE SYSOCAL ....  -2.53 uV
       relay closed, zero AFTER  SYSOCAL ....  -0.21 uV
       relay open,   signal BEFORE .......... +17.79 uV
       relay open,   signal AFTER  .......... +20.23 uV
       OFC after SYSOCAL .................... -2099

   17.79 - (-2.53) = 20.32 uV against a measured 20.23 uV: the offset
   removed and the signal recovered agree to 0.09 uV, which is the noise
   floor.  The correction is real, and the relay genuinely removes the
   differential signal when it closes.

  ---------------------------------------------------------------------
   What changed in v2.1
  ---------------------------------------------------------------------
     * Points are driven by the ADS1256, not by a clock.  v2.0 put a
       point on a fixed 500 ms schedule and averaged the five conversions
       that had arrived.  Asking that design for 100 ms points would have
       broken three ways: each slot would expect exactly one conversion,
       so a crystal half a percent slow leaves one slot in two hundred
       empty; SAMPLE_GRACE_MS (300 ms) could no longer be shorter than
       the interval, removing v1.3's stall protection; and the
       after-stall discard, which used to cost one conversion in five,
       would have cost whole points.  Now every fresh conversion IS one
       point, stamped with the millisecond it was read, so the converter
       sets the rate and jitter is recorded instead of losing data.
       ~10 points/s instead of ~2 - five times the spatial resolution
       while the operator walks.
     * The baseline window is TIME based (BASELINE_WINDOW_MS, 30 s), not
       a fixed number of readings, so changing the rate later cannot
       silently change the behaviour.  Held as 30 one-second buckets of
       sum + count: 180 bytes, against the 1200 bytes that storing every
       reading in the window would need.
     * If the operator stands over an anomaly for longer than the whole
       window, every bucket empties.  The baseline is then HELD at its
       last value rather than collapsing to zero, which would have made
       every later reading abnormal and left the alarm stuck on.
     * Initial baseline learning is time based too (INITIAL_LEARNING_MS,
       5 s -> about 50 readings at 10 Hz).
     * ALARM_CONFIRM_POINTS 3 -> 2.  At 10 Hz that is 0.2 s.
     * The CSV is flushed on a 2 s timer instead of after every row, and
       the write happens immediately after a conversion is read, where
       there is ~95 ms of slack before the next one.  A queue of pending
       rows was considered and rejected: it cannot prevent conversion
       loss, because if the program is inside a blocking write when DRDY
       falls the result is overwritten either way.  Dropped conversions
       are counted instead.

  ---------------------------------------------------------------------
   What changed in v1.4  (v1.3 was bench-proven on 20 Sep 2026)
  ---------------------------------------------------------------------
     * Serial command 'c' = SYSTEM ZERO CALIBRATION (ADS1256 SYSOCAL).
       Why: on 20 Sep, with the KORAD bench supply driving a measured
       151 kOhm / 99.7 Ohm divider, the unit read 1.83577 mV against a
       theoretical 1.83894 mV at the top of the client's range - 0.17 %,
       excellent.  But with CENTER shorted to SHELL, which forces the
       true input to 0 uV, it read -5.894 uV and that offset wandered by
       1.88 uV inside 48 seconds.  At the client's 20 uV minimum the
       +/-10 % alarm band is only +/-2 uV, so an offset of that size,
       drifting by that much, sits right on top of the alarm band.
       SELFCAL (still issued at start-up) only removes the CONVERTER's
       own offset and gain error.  It cannot see the front end, the
       wiring or the breadboard junctions, and that is where most of the
       -5.9 uV comes from.  SYSOCAL measures whatever is actually present
       at the input pins at that moment and stores it as the offset
       calibration, so it cancels the whole signal chain.
       In v1.4 this was manual, precisely because a real antenna signal
       present at power-up would have been calibrated away as "zero".
       v2.2 makes it automatic by guaranteeing, with the relay, that the
       input really is shorted - and still checks before trusting it.
     * A SYSOCAL is marked invalid the moment the ADS1256 is
       re-initialised, and the operator is told in plain words.
       adsInitialise() is reached three ways: at start-up, from the 'r'
       command, and - the one that bites - from the automatic background
       retry in updateAdcHealth(), which runs with nobody watching.  All
       three hard power-cycle the chip, send RESET and then SELFCAL, and
       every one of those replaces the offset value SYSOCAL established.

  ---------------------------------------------------------------------
   What changed in v1.3  (v1.2 was bench-proven on 18 Sep 2026)
  ---------------------------------------------------------------------
     * A slow microSD write no longer costs a reading; the stall
       protection waits for the next conversion instead of losing it.
     * Failed SD writes are detected and announced.  A silent logging
       failure is the one fault that would let the operator finish a
       survey believing the data was saved.
     * The sketch no longer freezes in setup() when the ADS1256 does not
       answer.  It reports the fault, keeps the buttons and Serial
       Monitor alive, retries in the background and recovers by itself,
       and refuses to record while the ADC is faulty.
     * Beeps no longer stop the program (no delay() in the feedback path).
     * DRDY has a pull-up, so an unplugged data-ready wire reads as
       "no data" instead of floating and producing nonsense.
     * A watchdog restarts the unit if the program ever locks up, and
       says so on the Serial Monitor after the restart.
     * ADC input saturation (antenna unplugged, wiring fault) is
       detected and reported.
     * Version, build date and free memory are printed at start-up.

  ---------------------------------------------------------------------
   Wiring summary (full details in the wiring guide)
  ---------------------------------------------------------------------
     Arduino pin   ->  what
     D2            ->  THRESHOLD button (other leg to GND)
     D3            ->  ZERO-CALIBRATION RELAY driver
                         D3 -> 1 k -> 2N2222 base
                         2N2222 base -> 13 k -> GND   (hold-off)
                         +5V -> relay coil -> 2N2222 collector
                         2N2222 emitter -> GND
                         1N4005 across the coil, BANDED end to +5V
                       relay contacts across CENTER and SHELL
     D4            ->  START button (other leg to GND)
     D5            ->  STOP  button (other leg to GND)
     D6            ->  Green LED (through a resistor, LED - leg to GND)
     D7            ->  Red LED   (same)
     D8            ->  Buzzer module I/O (passive, LOW-trigger module)
     D9            <-  ADS1256 DRDY
     D10           ->  ADS1256 CS
     D11 (MOSI)    ->  ADS1256 DIN    and  SD breakout DI
     D12 (MISO)    <-  ADS1256 DOUT   and  SD breakout DO
     D13 (SCK)     ->  ADS1256 SCLK   and  SD breakout CLK
     A0            ->  NOT USED (old analogue knob disconnected)
     A1            ->  SD breakout CS
     A2            ->  ADS1256 PDWN (labelled SYNC on some boards)
     A3            ->  NOT USED.  This ADS1256 module has no RESET pin;
                       the sketch uses the software RESET command 0xFE.
     5V / GND      ->  ADS1256, SD breakout, relay coil, buttons, LEDs

     Analogue front end:
               5V -> 10 k -> BIAS -> 10 k -> GND
               10 uF from BIAS to GND      (BIAS measures about 2.52 V)
               SHELL  -> BIAS
               CENTER -> 1 k -> AIN0
               SHELL  -> 1 k -> AIN1
               10 nF directly between the AIN0 and AIN1 nodes
     The ~2.5 V bias is COMMON MODE.  The ADS1256 measures AIN0 - AIN1
     only, so the bias is not part of the reading; it exists to hold the
     floating antenna inside the converter's input range.

     ADS1256 configuration (bench-proven):
               differential AIN0 - AIN1,  PGA = 64,  input buffer ON,
               10 SPS,  VREF ~ 2.500 V,  SPI_MODE1 at 1 MHz.

   IMPORTANT: switch the unit OFF before the microSD card goes in or
   out.  Inserting a card into a powered breakout can corrupt it.

   The ADC and the SD card can both be missing: the sketch says so at
   start-up, keeps running, and picks them up when they appear (with
   TEST_MODE 1 it simulates the ADC).  Settings you may want to change
   are all in the SETTINGS block below.
  =====================================================================
*/

#include <SPI.h>
#include <SD.h>
#include <avr/wdt.h>

#define FIRMWARE_VERSION "v3.2"
#define FIRMWARE_BUILD_DATE "25 Sep 2026"

// =====================================================================
//  SETTINGS  (the only block you normally need to touch)
// =====================================================================

// Set to 1 to run WITHOUT the ADS1256 connected.  The sketch then makes
// up a realistic antenna reading (about 0.072 mV with noise and slow
// drift, like the client's own data) so you can test the buttons, LEDs,
// buzzer and SD card.  While SCANNING in test mode you can fake a
// "buried object" by typing a in the Serial Monitor.  Repeated presses
// cycle OFF -> +20 % -> +60 % -> +120 % -> OFF, which verifies the
// 10 %, 50 % and 100 % button settings.  Set to 0 for the real device.
#ifndef TEST_MODE
#define TEST_MODE 0
#endif

// ---- Alarm rule ---------------------------------------------------
// Initial threshold on every power-up.  The D2 button cycles through
// 10 %, 50 %, and 100 % while idle.  The client's original rule was 10 %.
const float ALARM_PERCENT = 10.0;

// true  = alarm when the reading moves away from the baseline in EITHER
//         direction (client: "readings can either go up or down").
// false = alarm only when the reading goes ABOVE baseline + ALARM_PERCENT.
const bool ALARM_BOTH_DIRECTIONS = true;

// Safety floor for the alarm band, in mV.  If the baseline happens to be
// ~0 (for example inputs shorted on the bench) 10 % of it would be 0 and
// every sample would alarm.  0.001 mV = 1 microvolt.
const float MIN_THRESHOLD_MV = 0.001;

// How many ABNORMAL readings in a row before the buzzer and red LED come
// on, so one noisy sample cannot cry wolf.  The client asked for 3 when a
// point was half a second; at 10 points/s, 2 is 0.2 s and detects a
// half-second object - well under a metre of ground at walking pace.
// This delays only the ALARM.  Every abnormal reading is still logged as
// ABNORMAL straight away and is excluded from the baseline straight away,
// so a suspicious reading can never contaminate what the unit considers
// normal while it waits for confirmation.
const uint8_t ALARM_CONFIRM_POINTS = 2;

// ---- Baseline learning and the rolling window ---------------------
// Everything here is in TIME, not in numbers of readings, so changing
// the measurement rate later does not silently change the behaviour.

// A short settle between START and the first reading that counts for
// anything.  The START tone alone is 250 ms, and the first bench scan
// opened 0.03708, 0.03148, 0.02344, 0.00390 mV before settling near
// 0.023 - transients from the buzzer and the button, not the ground.
// Letting those into the baseline biases it and inflates the spread.
// They are still logged, as SETTLING, so nothing is hidden.
const unsigned long SETTLE_MS = 1000;

// How long after the settle the unit spends learning the starting
// baseline.  The alarm is held off for this period too.
const unsigned long INITIAL_LEARNING_MS = 5000;

// The baseline is the average of the readings ACCEPTED AS NORMAL in the
// last BASELINE_WINDOW_MS.  Older readings stop counting, so the unit
// follows ground that genuinely changes as the operator walks, instead
// of being anchored by readings from twenty minutes ago.
const unsigned long BASELINE_WINDOW_MS = 30000;

// Storing every reading in the window would cost 30 s x 10 Hz x 4 bytes
// = 1200 bytes of SRAM, which an UNO cannot spare.  Instead the window
// is kept as one-second buckets, each holding a sum and a count: 30
// buckets x 6 bytes = 180 bytes, and the arithmetic is identical apart
// from the one-second granularity at which readings leave the window.
const unsigned long BASELINE_BUCKET_MS = 1000;
const uint8_t BASELINE_BUCKETS = (uint8_t)(BASELINE_WINDOW_MS / BASELINE_BUCKET_MS);

// ---- Automatic zero calibration -----------------------------------
// How long the analogue path is left to settle with the relay CLOSED
// before SYSOCAL runs.  The green LED blinks throughout.  The bench test
// on 21 Sep 2026 worked with far less, but start-up time is not worth
// optimising: 5 s costs nothing and guarantees the 10 nF across the
// inputs and the 10 uF on the bias point have finished moving.
const unsigned long CAL_SETTLE_MS = 5000;

// If the "shorted" reading is bigger than this, the relay did NOT short
// the input - a stuck contact, a broken coil or a broken wire - and
// SYSOCAL would calibrate a real antenna signal away as zero, so it is
// skipped.  Well above the few microvolts of system offset (the measured
// relay-closed zero on 21 Sep was -2.53 uV), well below a healthy
// antenna signal (the client's range starts at 20 uV).
constexpr float CAL_SANITY_MV = 0.010;

// A calibration is accepted only if the measured residual afterwards is
// no larger than this.  0.002 mV = 2 microvolts, equal to the client's
// original 10 % band at the specified 0.020 mV minimum signal.
constexpr float CAL_VERIFY_MV = 0.002;

// Conversions thrown away after the relay moves, before anything is
// believed.  The relay contacts settle in about a millisecond, but the
// 10 nF across AIN0/AIN1 and the ADS1256's own digital filter need
// several conversion periods to follow the step.  5 at 10 SPS = 0.5 s,
// which is what the successful 21 Sep relay/SYSOCAL test used.
const uint8_t RELAY_SETTLE_CONVERSIONS = 5;

// ---- Threshold selection button -----------------------------------
// A normally-open button connects D2 to GND when pressed.  INPUT_PULLUP
// supplies the pull-up; no external resistor or 5 V button wire is needed.
// Selection is ignored while recording, so an active scan cannot change.
const uint8_t THRESHOLD_PERCENTAGES[3] = {10, 50, 100};

// Field operation requires a persistent CSV.  Set this to false only for
// controlled bench work where Serial-only operation is intentional.
const bool REQUIRE_SD_FOR_SCAN = true;

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
const unsigned int ALARM_BEEP_ON_MS = 50;
const unsigned int ALARM_BEEP_PERIOD_MS = 1000;

// ---- Sampling -----------------------------------------------------
// There is no sample interval any more.  Points are not produced on a
// clock; every fresh ADS1256 conversion IS one point, timestamped with
// the moment it was actually read.  At DRATE 0x23 (10 SPS) that gives
// about 10 points per second, and the logged time is what really
// happened rather than what a schedule predicted.

// Nominal gap between conversions at 10 SPS, used only to notice when
// conversions have been missed and when the converter has gone quiet.
const unsigned long ADC_NOMINAL_MS = 100;

// No conversion for this long means the ADS1256 has stopped talking.
const unsigned long ADC_SILENT_MS = 1500;

// A gap longer than this means at least one conversion was missed while
// the program was busy elsewhere (almost always a slow SD-card write).
const unsigned long ADC_MISS_MS = 150;

// How often the CSV file is pushed onto the card.  Rows are written as
// they happen, but a flush also rewrites the directory entry, which is
// the slow part - doing that ten times a second would be what finally
// made the unit miss conversions.  Twenty seconds greatly reduces the
// forced-write disturbances, but risks up to 20 seconds of recent data
// if the battery is pulled mid-scan.
const unsigned long LOG_FLUSH_MS = 20000;

// How many conversions the 'c' command averages when it reports the zero
// before and after calibrating.  At 10 SPS, 10 points is about one second
// each side, which is enough to see past the noise without a long wait.
const uint8_t SYSOCAL_AVG_POINTS = 10;

// ---- Indicators ---------------------------------------------------
// Blink the green LED while calibrating and during settling/baseline
// learning.  Solid green = ready, or scanning normally.
const bool BLINK_GREEN_WHEN_BUSY = true;

// ---- Watchdog -----------------------------------------------------
// 1 = the hardware watchdog restarts the unit if the program ever stops
// responding (a locked-up SPI bus, for example).  Worth having in a
// battery-powered field instrument that nobody is watching.  Set to 0
// only if you are uploading to a board with an old bootloader that does
// not clear the watchdog flag - the Arduino UNO R3 is fine.
#define ENABLE_WATCHDOG 1

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
// D2 reads the threshold-selection button using INPUT_PULLUP.  One
// button leg goes to D2 and the other goes to GND.
//
// D3 is NOT spare.  It drives the automatic zero-calibration relay
// through a 1 k base resistor into a 2N2222, with a 13 k base pull-down
// to GND and a 1N4005 flyback diode across the coil.  The relay contacts
// sit across CENTER and SHELL, i.e. across the real antenna connector
// rather than directly across AIN0/AIN1, so that closing it reproduces
// the manual jumper that SYSOCAL was proven with and lets SYSOCAL cancel
// the front-end resistors and wiring as well as the converter.
const uint8_t PIN_BTN_THRESHOLD      = 2;
const uint8_t PIN_ZERO_RELAY         = 3;
const uint8_t PIN_BTN_START          = 4;
const uint8_t PIN_BTN_STOP           = 5;
const uint8_t PIN_LED_GREEN          = 6;
const uint8_t PIN_LED_RED            = 7;
const uint8_t PIN_BUZZER             = 8;
const uint8_t PIN_ADC_DRDY           = 9;
const uint8_t PIN_ADC_CS             = 10;
// D11 = MOSI, D12 = MISO, D13 = SCK  (fixed by the UNO hardware)
// A0 is deliberately unused after removal of the analogue knob.
const uint8_t PIN_SD_CS              = A1;
const uint8_t PIN_ADC_PDWN           = A2;
// A3 is deliberately unused.  Earlier versions drove it as an ADS1256
// RESET line; this module has no reset pin, so the sketch uses the
// software RESET command (0xFE) instead and leaves A3 alone.

// =====================================================================
//  INTERNAL CONSTANTS  (no need to change these)
// =====================================================================
const uint8_t  MV_DECIMALS        = 5;      // 0.00001 mV = 0.01 microvolt
const unsigned long IDLE_PRINT_MS = 2000;   // live reading on the Serial Monitor when idle
const unsigned long ADC_RETRY_MS  = 2000;   // how often to re-check a faulty ADC when idle
const unsigned long ADC_RETRY_BUSY_MS = 10000;  // ... and while a recording is running
const unsigned long FAULT_WINK_MS = 80;     // red LED wink length when something is wrong
const unsigned long FAULT_WINK_PERIOD_MS = 2000;
const uint16_t MAX_FILE_INDEX     = 9999;   // SCAN9999.CSV
const long     ADC_SATURATED_COUNTS = 8380000L;  // practically full scale for a 24-bit result
const uint8_t  ADC_SATURATION_CONFIRM_POINTS = 3;

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

// One step of a buzzer sequence: a pitch in Hz (0 = silence) for ms
// milliseconds.  A step with ms == 0 ends the sequence.
struct Note {
  uint16_t hz;
  uint16_t ms;
};

// Running figures for one recording: how many points, their average,
// and the smallest and largest of them.  The spread (largest minus
// smallest) is what tells the operator whether a baseline was steady.
struct Stats {
  unsigned long n;
  float sum;
  float minV;
  float maxV;
};

// CALIBRATING -> start-up SELFCAL + automatic relay-driven SYSOCAL
// READY       -> calibrated, waiting for START
// SETTLING    -> first SETTLE_MS after START; logged, counts for nothing
// LEARNING    -> next INITIAL_LEARNING_MS, builds the baseline, no alarm
// SCANNING    -> normal adaptive detection.  The alarm is a flag inside
//                this state, not a state of its own.
// The order matters: recording() tests phase >= SETTLING.
enum Phase { CALIBRATING, READY, SETTLING, LEARNING, SCANNING };

// =====================================================================
//  COMPILE-TIME CHECKS  (catch a bad setting before it reaches the bench)
// =====================================================================
static_assert(BASELINE_BUCKETS >= 2 && BASELINE_BUCKETS <= 60,
              "BASELINE_WINDOW_MS / BASELINE_BUCKET_MS must give 2..60 buckets");
static_assert(BASELINE_WINDOW_MS % BASELINE_BUCKET_MS == 0,
              "BASELINE_WINDOW_MS must be a whole number of buckets");
static_assert(ALARM_BEEP_ON_MS < ALARM_BEEP_PERIOD_MS,
              "ALARM_BEEP_ON_MS must be shorter than ALARM_BEEP_PERIOD_MS");
static_assert(ALARM_CONFIRM_POINTS >= 1,
              "ALARM_CONFIRM_POINTS must be at least 1");
static_assert(CAL_VERIFY_MV > 0.0 && CAL_VERIFY_MV < CAL_SANITY_MV,
              "CAL_VERIFY_MV must be above zero and below CAL_SANITY_MV");
static_assert(ADC_SATURATION_CONFIRM_POINTS >= 1,
              "ADC_SATURATION_CONFIRM_POINTS must be at least 1");
static_assert(sizeof(THRESHOLD_PERCENTAGES) == 3,
              "Threshold button needs exactly three settings");

// =====================================================================
//  WATCHDOG HELPERS
// =====================================================================
inline void watchdogKick() {
#if ENABLE_WATCHDOG
  wdt_reset();
#endif
}

// =====================================================================
//  ZERO-CALIBRATION RELAY
// =====================================================================
// LOW  = coil off = contacts OPEN   = CENTER and SHELL separate
//                                   = the antenna is connected, measure
// HIGH = coil on  = contacts CLOSED = CENTER shorted to SHELL
//                                   = the differential input is zero
//
// The 13 k base pull-down holds the transistor off while the MCU pin is
// floating during boot and reset, so the instrument's default state is
// "measuring", not "shorted", even before setup() has run.
bool relayClosed = false;

void relayOpen() {
  digitalWrite(PIN_ZERO_RELAY, LOW);
  relayClosed = false;
}

void relayClose() {
  digitalWrite(PIN_ZERO_RELAY, HIGH);
  relayClosed = true;
}

// =====================================================================
//  BUZZER  (one non-blocking engine; works for active and passive
//  buzzers, either trigger level)
// =====================================================================
// Brief, lower-pitched cues are less intrusive on the installed passive
// buzzer module.  The alarm and scan cues retain their original sound.
const Note SND_POWER_ON[]   PROGMEM = {{1050, 40}, {0, 55}, {1250, 40}, {0, 55}, {1450, 55}, {0, 0}};
const Note SND_CLICK[]      PROGMEM = {{2000, 35}, {0, 0}};
const Note SND_THRESHOLD_10[]  PROGMEM = {{1250, 35}, {0, 0}};
const Note SND_THRESHOLD_50[]  PROGMEM = {{1250, 35}, {0, 80}, {1250, 35}, {0, 0}};
const Note SND_THRESHOLD_100[] PROGMEM = {{1250, 35}, {0, 80}, {1250, 35},
                                          {0, 80}, {1250, 35}, {0, 0}};
const Note SND_CAL_OK[]     PROGMEM = {{1200, 45}, {0, 70}, {1200, 45}, {0, 0}};
const Note SND_SCAN_START[] PROGMEM = {{2200, 250}, {0, 0}};
// Baseline learned, alarm now live.  Deliberately RISING: the start tone
// is one flat note, the calibration tone is two equal beeps and the stop
// tone falls, so an operator who is walking and not looking can tell
// which of the four just happened.
const Note SND_ARMED[]      PROGMEM = {{2000, 80}, {0, 50}, {2800, 160}, {0, 0}};
const Note SND_SCAN_STOP[]  PROGMEM = {{2000, 90}, {0, 50}, {1500, 140}, {0, 0}};
const Note SND_ERROR[]      PROGMEM = {{3000, 60}, {0, 60}, {3000, 60}, {0, 60},
                                       {3000, 60}, {0, 60}, {3000, 60}, {0, 0}};

const Note   *seqNext      = NULL;   // next step of the sequence being played
bool          seqRunning   = false;
uint16_t      seqStepMs    = 0;      // length of the step now playing
unsigned long seqStepStart = 0;      // when that step started
uint16_t      toneNow      = 0;      // what the pin is doing right now (0 = silent)

// Drive the pin only when the pitch actually changes, so tone() is not
// restarted 30 000 times a second.
void applyTone(uint16_t hz) {
  if (hz == toneNow) return;
  toneNow = hz;
  if (hz == 0) {
#if BUZZER_PASSIVE
    noTone(PIN_BUZZER);                            // noTone() leaves the pin LOW ...
#endif
    digitalWrite(PIN_BUZZER, BUZZER_IDLE_LEVEL);   // ... so park it at the silent level
  } else {
#if BUZZER_PASSIVE
    tone(PIN_BUZZER, hz);
#else
    digitalWrite(PIN_BUZZER, BUZZER_IDLE_LEVEL == HIGH ? LOW : HIGH);
#endif
  }
}

void soundStart(const Note *seq) {
  seqNext      = seq;
  seqRunning   = true;
  seqStepMs    = 0;
  seqStepStart = millis();
}

void soundStop() {
  seqRunning = false;
  seqNext    = NULL;
  seqStepMs  = 0;
}

// Called from the main loop.  The alarm owns the buzzer while it is
// sounding; feedback sequences fill the silence the rest of the time.
bool alarmActive = false;                 // set by the scan logic below
unsigned long alarmStartedMs = 0;

void updateBuzzer() {
  if (alarmActive) {
    if (seqRunning) soundStop();
    unsigned long since = millis() - alarmStartedMs;
    bool on = ALARM_BEEPING ? ((since % ALARM_BEEP_PERIOD_MS) < ALARM_BEEP_ON_MS) : true;
    applyTone(on ? BUZZER_ALARM_HZ : 0);
    return;
  }
  if (!seqRunning) { applyTone(0); return; }
  if (millis() - seqStepStart < seqStepMs) return;         // current step still playing

  uint16_t hz = pgm_read_word(&seqNext->hz);
  uint16_t ms = pgm_read_word(&seqNext->ms);
  if (ms == 0) { soundStop(); applyTone(0); return; }      // end of the sequence
  applyTone(hz);
  seqStepStart += seqStepMs;                               // keeps the timing exact
  seqStepMs     = ms;
  seqNext++;
}

// Used for short acknowledgement sounds where the next action may block
// for several seconds.  Completing the click first makes a button press
// unmistakable even while SD.begin() or zero calibration is busy.
void soundPlayBlocking(const Note *seq) {
  soundStart(seq);
  while (seqRunning) { updateBuzzer(); watchdogKick(); }
}

// =====================================================================
//  ADS1256 DRIVER  (small, self-contained; no library needed)
// =====================================================================
// Commands (from the ADS1256 datasheet, Table 24)
const uint8_t ADS_CMD_RDATA   = 0x01;   // read one conversion result
const uint8_t ADS_CMD_SDATAC  = 0x0F;   // stop continuous mode
const uint8_t ADS_CMD_RREG    = 0x10;   // read register(s)
const uint8_t ADS_CMD_WREG    = 0x50;   // write register(s)
const uint8_t ADS_CMD_SELFCAL = 0xF0;   // self-calibrate offset + gain (the CHIP's own errors)
const uint8_t ADS_CMD_SYSOCAL = 0xF3;   // system offset calibration (the WHOLE chain's zero)
const uint8_t ADS_CMD_RESET   = 0xFE;

// Registers
const uint8_t ADS_REG_STATUS = 0x00;
const uint8_t ADS_REG_MUX    = 0x01;
const uint8_t ADS_REG_ADCON  = 0x02;
const uint8_t ADS_REG_DRATE  = 0x03;
// Offset-calibration registers: a 24-bit value the chip subtracts from every
// conversion.  SELFCAL and SYSOCAL both write it; reading it back is how we
// prove the calibration was actually accepted.
const uint8_t ADS_REG_OFC0   = 0x05;   // least significant byte
const uint8_t ADS_REG_OFC1   = 0x06;
const uint8_t ADS_REG_OFC2   = 0x07;   // most significant byte

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
    watchdogKick();
    // Every blocking ADC wait in the sketch comes through here, and all
    // of them are on calibration paths that can last a second or more.
    // Without this a feedback sound started just beforehand would hold
    // one note for the whole wait instead of finishing its sequence.
    updateBuzzer();
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

// Defined further down, where the recording state it needs is in scope.
void invalidateSystemZeroCalibration();
void actionStopScan();
void actionSystemZeroCalibrate();
bool automaticZeroCalibration();

// Power up, configure and self-calibrate the ADC.  Returns true if the
// chip answered correctly (we read back the registers we wrote).
// 'quiet' suppresses the register line during background retries.
bool adsInitialise(bool quiet) {
  // FIRST, before anything touches the chip: any SYSOCAL the operator set
  // is about to be destroyed, and it is destroyed whether or not the rest
  // of this function succeeds.  The hard power-down below and the RESET
  // command both restore the ADS1256's default offset registers, and the
  // SELFCAL at the end writes its own value over whatever was there.  So
  // the invalidation cannot wait for the SELFCAL line or sit on the
  // success path - an early return at the register check would leave the
  // operator believing in a calibration the reset had already wiped.
  invalidateSystemZeroCalibration();

  digitalWrite(PIN_ADC_PDWN, LOW);      // hard power-down ...
  delay(20);
  digitalWrite(PIN_ADC_PDWN, HIGH);     // ... and back up = clean start
  delay(50);
  watchdogKick();

  adsCommand(ADS_CMD_RESET);
  delay(10);
  adsCommand(ADS_CMD_SDATAC);           // make sure it is not streaming data
  delay(2);

  const uint8_t cfg[4] = {ADS_STATUS_VAL, ADS_MUX_VAL, ADS_ADCON_VAL, ADS_DRATE_VAL};
  adsWriteRegisters(ADS_REG_STATUS, cfg, 4);
  delay(2);

  uint8_t status = adsReadRegister(ADS_REG_STATUS);
  uint8_t mux    = adsReadRegister(ADS_REG_MUX);
  uint8_t adcon  = adsReadRegister(ADS_REG_ADCON);
  uint8_t drate  = adsReadRegister(ADS_REG_DRATE);

  if (!quiet) {
    Serial.print(F("ADS1256 registers: STATUS=0x"));         Serial.print(status, HEX);
    Serial.print(F(" MUX=0x"));                             Serial.print(mux, HEX);
    Serial.print(F(" ADCON=0x"));                            Serial.print(adcon, HEX);
    Serial.print(F(" DRATE=0x"));                            Serial.println(drate, HEX);
  }

  // STATUS[7:4] is the factory ID and STATUS[0] reflects DRDY.  Verify
  // only the writable ORDER/ACAL/BUFEN bits [3:1], especially BUFEN: a
  // failed buffer-enable write would materially load the antenna input.
  if ((status & 0x0E) != (ADS_STATUS_VAL & 0x0E) ||
      mux != ADS_MUX_VAL || adcon != ADS_ADCON_VAL || drate != ADS_DRATE_VAL) {
    return false;   // the chip is not talking to us (check wiring / power)
  }

  adsCommand(ADS_CMD_SELFCAL);          // remove the ADC's own offset and gain error
  delay(5);
  return adsWaitForDataReady(1500);     // self-calibration at 10 SPS takes well under a second
}

// ---------------------------------------------------------------------
//  SYSTEM OFFSET CALIBRATION  (SYSOCAL)
// ---------------------------------------------------------------------
// SELFCAL, above, corrects the converter's own offset and gain.  It runs at
// start-up and is left exactly as it was.  What it cannot correct is
// everything OUTSIDE the chip: the 1 kOhm front-end resistors, the bias
// network, the breadboard's nickel-clip-to-tinned-lead junctions and the
// wiring, which together produced a measured -5.894 uV on 20 Sep 2026.

// SYSOCAL fixes that by taking whatever is at the input pins RIGHT NOW to be
// zero and storing it in the offset-calibration registers.  So it is only
// correct if the input really is shorted when it runs.  In v1.4 to v2.1 that
// was the operator's job, with a jumper and a hand-typed command.  Since v2.2
// a reed relay across CENTER and SHELL does it, so the instrument calibrates
// its own zero at every power-up with nobody touching anything - and the
// sanity check below still refuses to calibrate if the relay did not
// actually short the input.

// Bench proof, 21 Sep 2026 (RadioShack 275-0232 / OMR-C-105H, coil 258.2 ohm):
//     relay closed, zero BEFORE SYSOCAL = -2.53 uV
//     relay closed, zero AFTER  SYSOCAL = -0.21 uV
//     relay open,   divider signal      = +20.23 uV   (was +17.79 uV before)
// The 2.53 uV that SYSOCAL removed and the 2.44 uV the signal rose by agree
// to 0.09 uV, which is the noise floor - the correction is real.

// Read the chip's 24-bit offset-calibration value (OFC2:OFC1:OFC0), sign
// extended.  Purely diagnostic - it is not used in the millivolt maths.
long adsReadOffsetCalibration() {
  uint32_t v =  (uint32_t)adsReadRegister(ADS_REG_OFC2) << 16;
  v         |= (uint32_t)adsReadRegister(ADS_REG_OFC1) << 8;
  v         |= (uint32_t)adsReadRegister(ADS_REG_OFC0);
  if (v & 0x800000UL) v |= 0xFF000000UL;      // 24-bit -> 32-bit sign extend
  return (long)v;
}

// Average 'n' fresh conversions, blocking.  Only safe to call when nothing is
// being recorded; the caller re-synchronises the sample schedule afterwards.
// Returns false if the ADC stopped answering part way through.
bool adsAverageBlocking(uint8_t n, float &avgMv) {
  double sum = 0.0;
  uint8_t got = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (!adsWaitForDataReady(1500)) break;     // kicks the watchdog while it waits
    long counts = adsReadConversion();
    // Do not let opposite-polarity rail hits cancel in the average and
    // masquerade as a good zero calibration.
    if (counts >= ADC_SATURATED_COUNTS || counts <= -ADC_SATURATED_COUNTS) {
      return false;
    }
    sum += adsCountsToMillivolts(counts);
    got++;
    watchdogKick();
  }
  if (got == 0) return false;
  avgMv = (float)(sum / got);
  return (got == n);
}

// Throw away 'n' fresh conversions.  Used after the zero-calibration
// relay moves: the contacts settle in about a millisecond, but the 10 nF
// across the inputs and the ADS1256's digital filter take several
// conversion periods to follow the step, so the first conversions after
// a relay change are part-way between the old value and the new one and
// must not be measured or calibrated against.
// Returns false if the converter stopped answering part way through.
bool adsDiscardConversions(uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    if (!adsWaitForDataReady(1500)) return false;
    adsReadConversion();
    watchdogKick();
  }
  return true;
}

// Issue SYSOCAL and wait for it to finish.  Returns false on timeout.
bool adsSystemOffsetCalibrate() {
  adsCommand(ADS_CMD_SDATAC);    // belt and braces: never calibrate while streaming
  delay(2);
  adsCommand(ADS_CMD_SYSOCAL);
  delay(5);
  // Calibration at 10 SPS takes a handful of conversion periods; DRDY goes
  // low when the new calibration value is in place.  3 s is generous.
  if (!adsWaitForDataReady(3000)) return false;
  // The conversion sitting in the register was taken during calibration, so
  // throw it away and let the next clean one through.
  adsReadConversion();
  return true;
}

// =====================================================================
//  BUTTONS  (with debounce: a press is only counted once)
// =====================================================================
Button btnStart = {PIN_BTN_START, true, true, 0};
Button btnStop  = {PIN_BTN_STOP,  true, true, 0};
Button btnThreshold = {PIN_BTN_THRESHOLD, true, true, 0};

const unsigned long DEBOUNCE_MS = 30;

// Returns true exactly once each time the button is pressed down.
bool buttonWasPressed(Button &b) {
  bool reading = (digitalRead(b.pin) == HIGH);
  if (reading != b.lastReading) {
    b.lastChangeMs = millis();
    b.lastReading  = reading;
  }
  if ((millis() - b.lastChangeMs) > DEBOUNCE_MS && reading != b.stableState) {
    b.stableState = reading;
    if (!b.stableState) return true;   // HIGH -> LOW = pressed
  }
  return false;
}

// =====================================================================
//  THRESHOLD BUTTON  ->  10 %, 50 %, or 100 %
// =====================================================================
uint8_t thresholdIndex = 0;             // power-on defaults to 10 %

float selectedAlarmPercent() {
  return (float)THRESHOLD_PERCENTAGES[thresholdIndex];
}

// =====================================================================
//  SYSTEM STATE
// =====================================================================
Phase phase = CALIBRATING;

// The baseline is the average of the readings ACCEPTED AS NORMAL in the
// last BASELINE_WINDOW_MS.  Abnormal readings are deliberately left out,
// so a sustained anomaly stays abnormal instead of slowly teaching the
// unit that it is the new normal - and old readings drop out of the far
// end, so the unit follows ground that genuinely changes.
bool  baselineReady = false;        // true once the learning period is over
float baselineMv    = 0.0;          // the value every reading is judged against
bool  baselineHeld  = false;        // window empty: baseline frozen, see below

// The window, as one-second buckets.  bSum/bCnt are the per-bucket
// totals, summed fresh on every point by baselineRecompute().
float         bSum[BASELINE_BUCKETS];
uint16_t      bCnt[BASELINE_BUCKETS];
uint8_t       bIdx     = 0;         // bucket currently being filled
unsigned long bStartMs = 0;         // when that bucket started
unsigned long winCount = 0;         // readings currently inside the window
float scanPercent     = ALARM_PERCENT;  // latched when a scan starts

unsigned long sampleNumber = 0;     // point number in the current scan

unsigned long phaseStartMs     = 0;   // time reference for the Time (s) column
unsigned long lastIdlePrintMs  = 0;
unsigned long lastConversionMs = 0;   // when the last fresh conversion arrived
unsigned long lastFlushMs      = 0;
float lastReadingMv = 0.0;
#if !TEST_MODE
bool drdyArmed = false;               // must see HIGH before accepting the next LOW
#endif

// Health and diagnostics
bool     adcOk            = false;
bool     sdReady          = false;
bool     sdFault          = false;
unsigned long adcRetryMs  = 0;
unsigned long droppedPoints   = 0;   // points lost in the current recording
unsigned long droppedTotal    = 0;   // points lost since power-on
unsigned long saturatedPoints = 0;   // invalid full-scale conversions in this scan
unsigned long saturatedTotal  = 0;   // invalid full-scale conversions since power-on
uint8_t  saturationRun        = 0;
bool     inputFault           = false;
unsigned long adcResets       = 0;
unsigned long sysocalCount    = 0;   // how many times 'c' has been run since power-on
bool     sysocalDone      = false;   // a SYSOCAL is currently in force
bool     sysocalLost      = false;   // one was set, then wiped by a re-init
float    sysocalZeroBefore = 0.0;    // measured zero just before the last SYSOCAL, mV
float    sysocalZeroAfter  = 0.0;    // measured zero just after it, mV

// How the scan ended.  One explicit field beats a VOID line that only
// appears on failure: a successful scan should say so just as plainly.
const uint8_t SCAN_RUNNING = 0, SCAN_COMPLETE = 1, SCAN_ADC_FAULT = 2,
              SCAN_EARLY_STOP = 3, SCAN_INPUT_FAULT = 4, SCAN_SD_FAULT = 5;
uint8_t  scanStatus  = SCAN_RUNNING;

uint8_t  abnormalRun = 0;            // ABNORMAL readings in a row, right now
int8_t   abnormalSide = 0;           // -1 below limit, +1 above limit
unsigned long alarmPoints = 0;       // points logged with the alarm ON
// Counted separately by phase.  Lumping the learning readings in with
// the scanning ones made "normal_points" mean two different things at
// once: the first bench file reported 193 normal points when about 51
// of them were baseline-learning readings that were never judged.
unsigned long settlingPoints = 0;
unsigned long learningPoints = 0;
unsigned long scanNormal     = 0;    // judged NORMAL while SCANNING
unsigned long scanAbnormal   = 0;    // judged ABNORMAL while SCANNING
unsigned long alarmEvents = 0;       // times the alarm went from off to on
unsigned long firstAlarmPoint = 0, lastAlarmPoint = 0;
float initialBaselineMv = 0.0;       // the baseline the moment learning ended
float maxAbsDelta = 0.0, maxDeltaPct = 0.0;
bool  maxDeltaPctValid = false;
unsigned long maxDeltaPoint = 0;

#if TEST_MODE
uint8_t simulatedAnomalyLevel = 0;   // 0=off, then 20 %, 60 %, 120 %
#endif

// True when a valid zero calibration is missing and the unit is not in
// the middle of producing one.  The instrument is then not fit to scan,
// which is why it counts as a fault for the indicator LEDs.
bool zeroCalMissing() { return !sysocalDone && phase != CALIBRATING; }

bool faultActive() { return sdFault || !adcOk || inputFault || zeroCalMissing(); }

// True while a scan is in progress, in any of its three states.
bool recording() { return phase >= SETTLING; }

// Alarm band for the CURRENT baseline.  With a positive baseline this is
// exactly the client's  baseline x 0.90 .. baseline x 1.10;  fabs() is
// used so that a negative baseline still gives low < high instead of
// silently swapping the two limits.
void alarmLimits(float base, float percent, float &thr, float &low, float &high) {
  thr = (percent / 100.0) * fabs(base);
  if (thr < MIN_THRESHOLD_MV) thr = MIN_THRESHOLD_MV;
  low  = base - thr;
  high = base + thr;
}

// =====================================================================
//  ROLLING BASELINE  (time-based, held as one-second buckets)
// =====================================================================
// Recomputed from the buckets every point rather than carried as a
// running total.  On AVR a double is only a 32-bit float, so a total
// that is added to and subtracted from ten times a second for an hour
// would slowly accumulate rounding error.  Thirty float additions at
// 10 Hz costs about 0.03 % of the processor and cannot drift.
void baselineRecompute() {
  float sum = 0.0;
  unsigned long cnt = 0;
  for (uint8_t i = 0; i < BASELINE_BUCKETS; i++) { sum += bSum[i]; cnt += bCnt[i]; }
  winCount = cnt;
  // An empty window means the operator has been over an anomaly for
  // longer than BASELINE_WINDOW_MS, so every normal reading has aged
  // out.  HOLD the last baseline instead of letting it fall to zero: a
  // zero baseline would make every later reading abnormal and the alarm
  // would never clear again.
  baselineHeld = (cnt == 0);
  if (!baselineHeld) baselineMv = sum / (float)cnt;
}

void baselineReset() {
  for (uint8_t i = 0; i < BASELINE_BUCKETS; i++) { bSum[i] = 0.0; bCnt[i] = 0; }
  bIdx = 0; bStartMs = millis(); winCount = 0;
  baselineMv = 0.0; baselineReady = false; baselineHeld = false;
}

// Retire buckets that have fallen out of the window.  Called on EVERY
// point, not only accepted ones - otherwise time would stop advancing
// while the operator stands over an anomaly.
void baselineAgeOut(unsigned long now) {
  if ((now - bStartMs) >= BASELINE_WINDOW_MS) {
    for (uint8_t i = 0; i < BASELINE_BUCKETS; i++) { bSum[i] = 0.0; bCnt[i] = 0; }
    bIdx = 0; bStartMs = now;
  } else {
    while ((now - bStartMs) >= BASELINE_BUCKET_MS) {
      bIdx = (uint8_t)((bIdx + 1) % BASELINE_BUCKETS);
      bSum[bIdx] = 0.0; bCnt[bIdx] = 0;      // reused as the newest bucket
      bStartMs += BASELINE_BUCKET_MS;
    }
  }
  baselineRecompute();
}

// Let one accepted NORMAL reading into the window.
void baselineAccept(float mv) {
  bSum[bIdx] += mv;
  if (bCnt[bIdx] < 65535) bCnt[bIdx]++;
  baselineRecompute();
}

// =====================================================================
//  RUNNING STATISTICS
// =====================================================================
// Learning readings and scanning readings are kept apart.  One set
// covering both made min/max/spread describe the start-up transient
// rather than the ground the operator actually walked.
Stats baseStats, scanStats;

void statsReset(Stats &s) {
  s.n = 0; s.sum = 0.0; s.minV = 0.0; s.maxV = 0.0;
}

void statsAdd(Stats &s, float x) {
  if (s.n == 0) { s.minV = x; s.maxV = x; }
  else { if (x < s.minV) s.minV = x; if (x > s.maxV) s.maxV = x; }
  s.n++;
  s.sum += x;
}

float statsMean(const Stats &s)   { return s.n ? s.sum / (float)s.n : 0.0; }
float statsSpread(const Stats &s) { return s.n ? s.maxV - s.minV : 0.0; }

// =====================================================================
//  SD CARD LOGGING  -  one CSV file per scan
// =====================================================================
File logFile;
bool logOpen = false;
char logFileName[13] = "";                // file currently being written
uint16_t nextScanIndex = 0;               // 0 = "not looked at the card yet"

// Try to start the SD card (also used to retry later if no card at power-up)
bool sdStart() {
  if (sdReady) return true;
  sdReady = SD.begin(PIN_SD_CS);
  if (sdReady) {
    nextScanIndex = 0;
    sdFault = false;
  } else {
    sdFault = true;
  }
  return sdReady;
}

// Something went wrong while writing.  Say so loudly: a silent logging
// failure is the one fault that would let the operator finish a survey
// believing the data was saved.
void sdWriteFailed() {
  Serial.println(F("*** SD WRITE FAILED - NOT being saved. Serial only. Stop and check the card."));
  if (logOpen) { logFile.clearWriteError(); logFile.close(); }
  logOpen = false;
  logFileName[0] = 0;
  sdReady = false;                        // force SD.begin() again next time
  sdFault = true;
  // A failure can first appear on the final flush after phase has already
  // returned to READY.  Preserve the truthful result for Serial even if
  // the summary could not be completed on the card.
  if (scanStatus == SCAN_RUNNING || scanStatus == SCAN_COMPLETE ||
      scanStatus == SCAN_EARLY_STOP) scanStatus = SCAN_SD_FAULT;
  soundStart(SND_ERROR);
  if (REQUIRE_SD_FOR_SCAN && recording()) {
    actionStopScan();
  }
}

// Called straight after a conversion has been read, when there is the
// most time before the next one is due.
bool logFlushChecked();

void logFlushIfDue() {
  if (!logOpen) return;
  if (millis() - lastFlushMs < LOG_FLUSH_MS) return;
  lastFlushMs = millis();
  logFlushChecked();
}

// Push the last rows onto the card and check that they arrived.
bool logFlushChecked() {
  if (!logOpen) return true;
  logFile.flush();
  if (logFile.getWriteError()) { sdWriteFailed(); return false; }
  return true;
}

// Build "PREF0001.CSV" into logFileName.  Done by hand rather than with
// snprintf, which would pull the whole printf engine (about 1 kB of
// program memory) into a sketch that has no other use for it.  The name
// is exactly 8.3 characters, which is all the SD library accepts.
void makeFileName(const char *prefix, uint16_t n) {
  for (uint8_t i = 0; i < 4; i++) logFileName[i] = prefix[i];
  logFileName[4]  = '0' + (n / 1000) % 10;
  logFileName[5]  = '0' + (n / 100)  % 10;
  logFileName[6]  = '0' + (n / 10)   % 10;
  logFileName[7]  = '0' +  n         % 10;
  logFileName[8]  = '.';
  logFileName[9]  = 'C';
  logFileName[10] = 'S';
  logFileName[11] = 'V';
  logFileName[12] = 0;
}

// Open a new file  PREFIX0001.CSV, PREFIX0002.CSV ...  (prefix = 4 letters)
bool logOpenNew(const char *prefix, uint16_t &nextIndex) {
  logFileName[0] = 0;
  if (!sdStart()) return false;
  if (nextIndex == 0) nextIndex = 1;
  while (nextIndex <= MAX_FILE_INDEX) {
    makeFileName(prefix, nextIndex);
    if (!SD.exists(logFileName)) break;
    nextIndex++;
    watchdogKick();
  }
  if (nextIndex > MAX_FILE_INDEX) {
    Serial.print(prefix);
    Serial.println(F(" numbers all used - copy the files off, clear the card."));
    logFileName[0] = 0;
    sdFault = true;
    return false;
  }
  logFile = SD.open(logFileName, FILE_WRITE);
  logOpen = (bool)logFile;
  if (!logOpen) {
    logFileName[0] = 0;
    sdReady = false;                      // make the next attempt re-mount the card
    sdFault = true;
    soundStart(SND_ERROR);
    return false;
  }
  nextIndex++;                            // next file starts after this one
  return true;
}

void logClose() {
  if (logOpen) {
    logFile.flush();
    bool bad = logFile.getWriteError();
    logFile.close();
    logOpen = false;
    if (bad) sdWriteFailed();
  }
}

// ---------------------------------------------------------------------
//  FILE LAYOUT
// ---------------------------------------------------------------------
// The file is written as three parts so that it can be read by a person
// as well as charted by a machine:
//
//     a flat Parameter,Value list  - which unit, which firmware, the
//                                    rule in force, the calibration
//     [READINGS]                   - one row per reading, units in the
//                                    headings, 13 columns
//     [SUMMARY]                    - totals, written when the scan stops
//
// v2.3 used five bracketed blocks; v2.4 flattened the three settings
// blocks into one list because the label names carry the grouping and
// the blank lines and markers cost flash the readings columns needed.
//
// Two rules govern the layout.  Every label carries its unit, so no
// column needs explaining and nothing has to be remembered.  And the
// block markers are square-bracketed rather than "=== ... ===", because
// a cell beginning with "=" is a formula to Excel and would open as an
// error rather than as a heading.
//
// A blank line separates each block, which is what makes Excel's
// "select the columns and insert a chart" work cleanly on the readings.

// Seconds, printed from the millisecond timestamp with integer maths.
// float would be within its precision here, but this is exact, shorter
// in program memory, and cannot round 12.999 to 13.000.
// Write seconds with exactly three decimal places using integer maths.
// CSV contains semantic values only; visual alignment belongs in the
// spreadsheet viewer, not as leading/trailing whitespace in cell data.
void printSeconds(unsigned long ms) {
  logFile.print(ms / 1000UL);
  logFile.print('.');
  unsigned int r = (unsigned int)(ms % 1000UL);
  if (r < 100) logFile.print('0');
  if (r < 10)  logFile.print('0');
  logFile.print(r);
}

// The fixed rows are one packed PROGMEM block rather than twenty
// print() calls.  The text costs the same either way; the call sites do
// not, and on a sketch at 97 % of flash that difference buys the
// Meaning column.
static const char CSV_FIXED[] PROGMEM =
  "MILLIVOLT MEASUREMENT AND ALARM SYSTEM\r\n"
  "Soil scan log\r\n"
  "\r\n"
  "Parameter,Value\r\n";

#if TEST_MODE
static const char CSV_SOURCE[] PROGMEM =
  "Data_Source,SIMULATED_TEST_MODE\r\n"
  "Measurement_ADC,SIMULATED\r\n"
  "Input_Mode,INTERNAL_TEST_GENERATOR\r\n"
  "ADC_Gain,";
#else
static const char CSV_SOURCE[] PROGMEM =
  "Data_Source,ADS1256_HARDWARE\r\n"
  "Measurement_ADC,ADS1256\r\n"
  "Input_Mode,AIN0-AIN1 differential\r\n"
  "ADC_Gain,";
#endif

static const char CSV_RULE[] PROGMEM =
  "Baseline_Method,Adaptive - only NORMAL readings update it\r\n";

// Five empty measurement fields before scanning begins.  Four commas
// separate them; the caller writes the fifth delimiter before Phase.
static const char CSV_GAP[] PROGMEM = ",,,,";

static const char CSV_COLS[] PROGMEM =
  "\r\n[READINGS]\r\n"
  "Point,Time_s,Reading_mV,Baseline_mV,Thresh_mV,Lower_mV,Upper_mV,"
  "Diff_mV,Diff_pct,Phase,Classification,AbnRun,Alarm\r\n";

static const char LBLS[] PROGMEM =
  "File_Name\0"
  "Firmware_Version\0"
  "Firmware_Build_Date\0"
  "Alarm_Threshold_Percent\0"
  "Settling_Time_s\0"
  "Baseline_Learning_Time_s\0"
  "Baseline_Window_s\0"
  "Alarm_Confirmation_Points\0"
  "Zero_Calibration\0"
  "Scan_Status\0"
  "Scan_Duration_s\0"
  "Total_Points\0"
  "Settling_Points\0"
  "Baseline_Learning_Points\0"
  "Scan_Normal_Points\0"
  "Scan_Abnormal_Points\0"
  "Alarm_Events\0"
  "Alarm_Points\0"
  "Dropped_Points\0"
  "Initial_Baseline_mV\0"
  "Final_Baseline_mV\0"
  "Baseline_Change_mV\0"
  "Baseline_Learning_Spread_mV\0"
  "Scan_Minimum_mV\0"
  "Scan_Maximum_mV\0"
  "Scan_Spread_mV\0"
  "Maximum_Deviation_mV\0"
  "First_Alarm_Point\0"
  "Last_Alarm_Point\0"
  "Maximum_Deviation_percent\0"
  "Maximum_Deviation_Point\0"
  "Nominal_Sample_Rate_Hz\0"
  "Alarm_Direction\0"
  "Saturated_Points\0"
  "Minimum_Threshold_mV\0";

// Walk the packed table to label n and write "Label,".  Fifty separate
// print(F("Label,")) call sites cost about eight bytes each in code on
// top of the text itself; at 97 % of flash that overhead is the
// difference between this file format fitting and not fitting.
void lbl(uint8_t n) {
  const char *q = LBLS;
  while (n--) { while (pgm_read_byte(q)) q++; q++; }
  logFile.print((const __FlashStringHelper *)q);
  logFile.print(',');
}

void logScanHeader() {
  if (!logOpen) return;
  // Derived from the settings that are actually in force.  A frozen
  // literal here would let the file certify a rule the firmware is not
  // running - which is the exact failure this version set out to fix.
  logFile.print((const __FlashStringHelper *)CSV_FIXED);
  logFile.print((const __FlashStringHelper *)CSV_SOURCE);
  logFile.println(ADC_PGA, 0);
  lbl(31); logFile.println(1000UL / ADC_NOMINAL_MS);
  lbl(0);                 logFile.println(logFileName);
  lbl(1);          logFile.println(F(FIRMWARE_VERSION));
  // Named for what it is.  An Arduino has no clock, so this is the fixed
  // release-build date, NOT the day the scan was taken - and a
  // bare "firmware,v2.3 Sep 22 2026" invites exactly that mistake.
  lbl(2);       logFile.println(F(FIRMWARE_BUILD_DATE));
  lbl(3);   logFile.println(scanPercent, 1);
  lbl(34);  logFile.println(MIN_THRESHOLD_MV, MV_DECIMALS);
  lbl(32);
  logFile.println(ALARM_BOTH_DIRECTIONS ? F("Above and below") : F("Above only"));
  logFile.print((const __FlashStringHelper *)CSV_RULE);
  lbl(4);           logFile.println(SETTLE_MS / 1000.0, 1);
  lbl(5);  logFile.println(INITIAL_LEARNING_MS / 1000.0, 1);
  lbl(6);         logFile.println(BASELINE_WINDOW_MS / 1000.0, 1);
  lbl(7); logFile.println(ALARM_CONFIRM_POINTS);
  lbl(8);
  #if TEST_MODE
  logFile.println(F("SIMULATED"));
  #else
  logFile.println(sysocalDone ? F("PASS") : F("FAIL"));
  #endif
  logFile.print((const __FlashStringHelper *)CSV_COLS);
  logFlushChecked();
}

// =====================================================================
//  PRINTING HELPERS
// =====================================================================
void printMv(Print &out, float mv) { out.print(mv, MV_DECIMALS); }

void printAlarmRule(float percent) {
  Serial.print(ALARM_BOTH_DIRECTIONS ? F("Alarm rule: outside baseline +/- ")
                                     : F("Alarm rule: above baseline + "));
  Serial.print(percent, 1); Serial.print(F(" %"));
  // Limits only while a scan is running.  After STOP, baselineReady is
  // still true and baselineMv still holds the LAST scan's baseline, so
  // printing limits then - e.g. when selection changes between scans -
  // would quote numbers the next scan will never use.
  if (baselineReady && recording()) {
    float thr, low, high;
    alarmLimits(baselineMv, percent, thr, low, high);
    if (ALARM_BOTH_DIRECTIONS) {
      Serial.print(F("  ->  alarm if reading < ")); printMv(Serial, low);
      Serial.print(F(" mV or > "));                 printMv(Serial, high);
    } else {
      Serial.print(F("  ->  alarm if reading > ")); printMv(Serial, high);
    }
    Serial.print(F(" mV"));
  }
  Serial.println();
}

void actionCycleThreshold() {
  if (recording()) {
    Serial.println(F("Threshold unchanged during scan; press after STOP."));
    return;
  }
  if (++thresholdIndex == 3) thresholdIndex = 0;
  printAlarmRule(selectedAlarmPercent());
  // Finish the count before another feedback sound can replace it.
  if      (thresholdIndex == 0) soundPlayBlocking(SND_THRESHOLD_10);
  else if (thresholdIndex == 1) soundPlayBlocking(SND_THRESHOLD_50);
  else                          soundPlayBlocking(SND_THRESHOLD_100);
}

void printHelp() {
  Serial.println(F("D2 THRESHOLD; D4 START; D5 STOP / idle RE-ZERO."));
  Serial.println(F("THRESHOLD: 1 beep=10%, 2=50%, 3=100%; idle only."));
  Serial.println(F("Serial: 1 start, 2 stop/re-zero, s status, c zero, r ADC, v version, ? help"));
#if TEST_MODE
  Serial.println(F("a: simulated anomaly OFF/+20/+60/+120 %."));
#endif
}

// Free SRAM, as a sanity check that the board is not running out of memory
int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

void printVersion() {
  Serial.print(F("FW ")); Serial.print(F(FIRMWARE_VERSION));
  Serial.print(' '); Serial.print(F(FIRMWARE_BUILD_DATE));
  Serial.print(F("; RAM ")); Serial.println(freeRam());
}

void printStatus() {
  // The CSV carries detailed totals; this is deliberately a compact live
  // health line so the Uno retains enough flash for the safety checks.
  Serial.print(F("State "));
  Serial.print(phase == CALIBRATING ? F("CAL") : phase == READY ? F("READY")
             : phase == SETTLING ? F("SETTLE") : phase == LEARNING ? F("LEARN")
                                                                   : F("SCAN"));
  Serial.print(F("; pts ")); Serial.print(sampleNumber);
  Serial.print(F("; alarm ")); Serial.print(alarmActive ? F("ON") : F("off"));
  Serial.print(F("; ADC "));
#if TEST_MODE
  Serial.print(F("SIM"));
#else
  if (inputFault) Serial.print(F("SAT"));
  else Serial.print(adcOk ? F("ok") : F("FAIL"));
#endif
  Serial.print(F("; zero ")); Serial.print(sysocalDone ? F("ok") : F("FAIL"));
  Serial.print(F("; SD "));
  if (sdFault)       Serial.print(F("FAULT"));
  else if (!sdReady) Serial.print(F("none"));
  else if (logOpen)  Serial.print(logFileName);
  else               Serial.print(F("ok"));
  Serial.print(F("; drop/sat ")); Serial.print(droppedTotal);
  Serial.print('/'); Serial.println(saturatedTotal);
}

// Classification of one reading
// Phase is WHERE the scan is; classification is WHAT the reading was
// judged to be.  v2.3 put "LEARN" in the classification column, which
// made a phase look like a verdict and left no way to say "this reading
// was never judged at all".
const uint8_t PH_SETTLE = 0, PH_LEARN = 1, PH_SCAN = 2;

// One scan reading -> Serial and SD.  'base' and 'thr' are the values
// the point was actually JUDGED against, captured before any baseline
// update, so the file always shows the decision that was really made.
void logScanRow(unsigned long n, unsigned long tms, float mv, float base,
                float thr, uint8_t ph, bool abnormal, uint8_t run, bool alarm) {
  Serial.print(tms / 1000.0, 1);  Serial.print(F(" s  #"));
  Serial.print(n);                Serial.print(F("  "));
  printMv(Serial, mv);
  if (ph == PH_SETTLE)     Serial.println(F(" mV  settling"));
  else if (ph == PH_LEARN) Serial.println(F(" mV  learning"));
  else {
    // Compact at 10 Hz: a full-width line every point would take 7 ms of
    // the 100 ms budget and scroll far too fast to read.
    Serial.print(F(" mV  base=")); printMv(Serial, base);
    if (alarm)         Serial.println(F("  *** ALARM ***"));
    else if (abnormal) { Serial.print(F("  ABNORMAL ")); Serial.println(run); }
    else               Serial.println(F("  ok"));
  }

  if (!logOpen) return;

  logFile.print(n);                         logFile.print(',');
  printSeconds(tms);                        logFile.print(',');
  logFile.print(mv, MV_DECIMALS);           logFile.print(',');

  // A column is written only where it means something, and left BLANK
  // otherwise.  Writing 0.00000 for a limit that does not exist yet is
  // not a placeholder - it is a wrong number, and charting it drags the
  // axis to zero and flattens the whole scan.
  if (ph != PH_SETTLE) logFile.print(base, MV_DECIMALS);
  logFile.print(',');
  if (ph == PH_SCAN) {
    float diff = mv - base;
    logFile.print(thr, MV_DECIMALS);           logFile.print(',');
    if (ALARM_BOTH_DIRECTIONS) logFile.print(base - thr, MV_DECIMALS);
    logFile.print(',');
    logFile.print(base + thr, MV_DECIMALS);    logFile.print(',');
    logFile.print(diff, MV_DECIMALS);          logFile.print(',');
    // A percentage relative to zero is undefined, not 0 %.  Leave the
    // cell empty so spreadsheet software cannot mistake it for data.
    if (fabs(base) > 1e-9) logFile.print((diff / fabs(base)) * 100.0, 1);
  } else {
    logFile.print((const __FlashStringHelper *)CSV_GAP);
  }
  logFile.print(',');
  if (ph == PH_LEARN) logFile.print(F("BASELINE_LEARNING"));
  else logFile.print(ph == PH_SETTLE ? F("SETTLING") : F("SCANNING"));
  logFile.print(',');
  if (ph != PH_SCAN) logFile.print(F("NOT_EVALUATED"));
  else if (abnormal) logFile.print(F("ABNORMAL"));
  else               logFile.print(F("NORMAL"));
  logFile.print(',');
  logFile.print(run);                       logFile.print(',');
  logFile.println(alarm ? F("YES") : F("NO"));
  // NOT flushed here.  See logFlushIfDue(): flushing ten times a second
  // is what would finally make the unit miss conversions.
}

// =====================================================================
//  LED INDICATORS  (one place decides what both LEDs do)
// =====================================================================
void updateLeds() {
  bool green;
  if (phase == CALIBRATING)    green = BLINK_GREEN_WHEN_BUSY ? ((millis() % 600) < 300) : true;
  // SETTLING blinks like LEARNING: neither is armed, and a solid green
  // during the settle reads as "scanning normally" to the operator,
  // who would then start walking a second before anything is learned.
  else if (phase == LEARNING || phase == SETTLING)
                               green = BLINK_GREEN_WHEN_BUSY ? ((millis() % 200) < 100) : true;
  // Calibration failed and was not retried: the unit looks idle but will
  // refuse to scan.  A slow green blink plus the red fault wink says so
  // from across a field, where nobody is reading the Serial Monitor.
  else if (zeroCalMissing()) green = (millis() % 1600) < 200;
  else                         green = true;    // READY or SCANNING = solid

  bool red;
  if (alarmActive)        red = true;                    // steady red = this point is outside the limits
  else if (faultActive()) red = (millis() % FAULT_WINK_PERIOD_MS) < FAULT_WINK_MS;   // wink = something needs attention
  else                    red = false;

  digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,   red   ? HIGH : LOW);
}

// =====================================================================
//  SAMPLE SCHEDULING
// =====================================================================
// Start a fresh scan.  The Time (s) column is measured from here.
void startScanClock() {
  phaseStartMs  = millis();
  sampleNumber  = 0;
  droppedPoints = 0;
  saturatedPoints = 0;
  lastFlushMs   = millis();
  statsReset(baseStats);
  statsReset(scanStats);
  baselineReset();
}

// =====================================================================
//  SCAN START/STOP ACTIONS  (called by the buttons and Serial commands)
// =====================================================================
const __FlashStringHelper *scanStatusText() {
  switch (scanStatus) {
    case SCAN_COMPLETE:    return F("COMPLETE");
    case SCAN_ADC_FAULT:   return F("VOID_ADC_FAULT");
    case SCAN_EARLY_STOP:  return F("INCOMPLETE_EARLY_STOP");
    case SCAN_INPUT_FAULT: return F("VOID_INPUT_FAULT");
    case SCAN_SD_FAULT:    return F("VOID_SD_FAULT");
    default:               return F("RUNNING");
  }
}

void finishScanFile() {
  if (!logOpen) return;
  logFile.println();
  logFile.println(F("[SUMMARY]"));

  // Always an explicit verdict, success included.  v2.3 wrote a VOID
  // line only when something went wrong, so a good file said nothing
  // about itself and you had to infer health from an absence.
  lbl(9);
  logFile.println(scanStatusText());
  lbl(10);    printSeconds(millis() - phaseStartMs);
  logFile.println();
  // Counted by phase.  One "normal_points" covering both the learning
  // readings and the judged ones meant two different things at once.
  lbl(11);             logFile.println(sampleNumber);
  lbl(12);          logFile.println(settlingPoints);
  lbl(13); logFile.println(learningPoints);
  lbl(14);       logFile.println(scanNormal);
  lbl(15);     logFile.println(scanAbnormal);
  lbl(16);             logFile.println(alarmEvents);
  lbl(17);             logFile.println(alarmPoints);
  lbl(18);           logFile.println(droppedPoints);
  lbl(33);         logFile.println(saturatedPoints);
  logFile.println();

  // How much the definition of "normal" moved while the operator walked.
  if (baselineReady) {
    lbl(19);      logFile.println(initialBaselineMv, MV_DECIMALS);
    lbl(20);        logFile.println(baselineMv, MV_DECIMALS);
    lbl(21);       logFile.println(baselineMv - initialBaselineMv, MV_DECIMALS);
  }
  // If the antenna was waved about during learning, the spread says so.
  // Logged, not rejected - there is not enough real antenna data yet to
  // know where a limit belongs.
  if (baseStats.n) {
    lbl(22); logFile.println(statsSpread(baseStats), MV_DECIMALS);
  }
  logFile.println();

  // The scanning readings only - the part of the recording that is about
  // the ground rather than about the instrument starting up.
  if (scanStats.n) {
    lbl(23); logFile.println(scanStats.minV, MV_DECIMALS);
    lbl(24); logFile.println(scanStats.maxV, MV_DECIMALS);
    lbl(25);  logFile.println(statsSpread(scanStats), MV_DECIMALS);
  }
  logFile.println();

  // Where to look in the graph, not just how big it was.
  if (scanStats.n) {
    lbl(26);      logFile.println(maxAbsDelta, MV_DECIMALS);
    if (maxDeltaPctValid) { lbl(29); logFile.println(maxDeltaPct, 1); }
    lbl(30);   logFile.println(maxDeltaPoint);
    if (alarmPoints) {
      lbl(27); logFile.println(firstAlarmPoint);
      lbl(28);  logFile.println(lastAlarmPoint);
    }
  }
  logClose();
}

// Refuse to record anything while the converter is not answering: a file
// full of zeros looks like data and is worse than no file at all.
bool adcUsable() {
  if (adcOk && !inputFault) return true;
  if (inputFault) {
    Serial.println(F("ADC input at full scale - scan refused. Check antenna/bias wiring."));
    soundStart(SND_ERROR);
    return false;
  }
  Serial.println(F("ADS1256 not responding - scan refused. Check the wiring."));
  soundStart(SND_ERROR);
  return false;
}

// Begin a completely new scan.  Every scan starts from nothing: new file,
// point counter back to 1, a fresh baseline learned from its own first
// readings.  Nothing is carried over from the previous scan.
void actionStartScan() {
  if (recording()) { Serial.println(F("Already scanning.")); return; }

  // Acknowledge an idle START before doing anything that can take time.
  // This matters in a sealed, stand-alone instrument: the operator should
  // never have to wonder whether the button contact was recognised.
  soundPlayBlocking(SND_CLICK);

  if (!adcUsable()) return;

  // Check the required card before making the operator wait through a
  // five-second calibration retry.  logOpenNew() still performs the real
  // file-create check below.
  if (REQUIRE_SD_FOR_SCAN && !sdStart()) {
    Serial.println(F("START BLOCKED: SD card unavailable."));
    soundStart(SND_ERROR);
    return;
  }

  // A precision scan is meaningless without a valid zero, but a marginal
  // power-on check can succeed after the analogue path has warmed and
  // settled.  START therefore makes one safe, visible retry and continues
  // directly into the scan if it succeeds; no Serial command or second
  // button press is required.
  if (!sysocalDone) {
    Serial.println(F("START: retrying zero calibration."));
    if (!automaticZeroCalibration()) {
      Serial.println(F("START BLOCKED: zero calibration failed."));
      // Every false return from automaticZeroCalibration() has already
      // started SND_ERROR.  Do not turn one fault into two error patterns.
      return;
    }
  }

  alarmActive     = false;
  scanStatus      = SCAN_RUNNING;
  abnormalRun     = 0;
  abnormalSide    = 0;
  alarmPoints     = 0;
  settlingPoints  = 0;
  learningPoints  = 0;
  scanNormal      = 0;
  scanAbnormal    = 0;
  alarmEvents     = 0;
  firstAlarmPoint = lastAlarmPoint = 0;
  initialBaselineMv = 0.0;
  maxAbsDelta     = 0.0;
  maxDeltaPct     = 0.0;
  maxDeltaPctValid = false;
  maxDeltaPoint   = 0;
  scanPercent     = selectedAlarmPercent(); // FROZEN for this scan: file and rows agree
  digitalWrite(PIN_LED_RED, LOW);

  bool haveLog = logOpenNew("SCAN", nextScanIndex);
  if (haveLog) {
    logScanHeader();
    haveLog = logOpen;                  // header flush can expose a write fault
  }
  if (!haveLog && REQUIRE_SD_FOR_SCAN) {
    Serial.println(F("Scan refused: SD unavailable. Power off; check card."));
    soundStart(SND_ERROR);
    return;
  }

#if !TEST_MODE
  // Remove any conversion that completed while the filename/header was
  // being written.  The next DRDY edge is then point 1 after time zero.
  if (!adsDiscardConversions(1)) {
    adcOk = false;
    phase = SETTLING;
    startScanClock();
    scanStatus = SCAN_ADC_FAULT;
    actionStopScan();
    return;
  }
  drdyArmed = false;                  // require a new HIGH -> LOW readiness cycle
  lastConversionMs = millis();
#endif

  phase = SETTLING;
  startScanClock();
  Serial.println(F("SCAN START: settle, learn baseline, then arm."));
  if (haveLog) { Serial.print(F("Logging to ")); Serial.println(logFileName); }
  else Serial.println(F("(Serial only - SD requirement disabled)"));
  soundStart(SND_SCAN_START);
  printAlarmRule(scanPercent);    // the percentage this scan is frozen at
}

void actionStopScan() {
  if (!recording()) {
    Serial.println(F("Idle STOP: re-zeroing."));
    actionSystemZeroCalibrate();
    return;
  }
  if (scanStatus == SCAN_RUNNING) {
    scanStatus = (baselineReady && phase == SCANNING) ? SCAN_COMPLETE : SCAN_EARLY_STOP;
  }
  phase       = READY;
  alarmActive = false;
  abnormalRun = 0;
  abnormalSide = 0;

  Serial.print(F("Points ")); Serial.println(sampleNumber);
  if (baselineReady) {
    Serial.print(F("Baseline: ")); printMv(Serial, initialBaselineMv);
    Serial.print(F(" -> "));       printMv(Serial, baselineMv); Serial.println(F(" mV"));
  } else {
    Serial.println(F("Baseline not established."));
  }
  if (droppedPoints) { Serial.print(F("Dropped : ")); Serial.println(droppedPoints); }
  if (saturatedPoints) { Serial.print(F("Saturated: ")); Serial.println(saturatedPoints); }
  finishScanFile();
  Serial.print(F("SCAN ")); Serial.println(scanStatusText());
  if (logFileName[0]) { Serial.print(F("File    : ")); Serial.println(logFileName); }
  if (scanStatus == SCAN_COMPLETE || scanStatus == SCAN_EARLY_STOP) {
    Serial.println(F("READY."));
    soundStart(SND_SCAN_STOP);
  } else {
    Serial.println(F("FAULT: correct it before a new scan."));
    soundStart(SND_ERROR);
  }
}

// =====================================================================
//  SYSTEM ZERO CALIBRATION  (Serial command 'c')
// =====================================================================

// Called from adsInitialise() on EVERY re-initialisation: the 'r' command,
// and - the dangerous one - the automatic background retry in
// updateAdcHealth(), which runs with no operator involved and deliberately
// keeps trying during a recording (ADC_RETRY_BUSY_MS).  Without this, a
// glitch mid-scan would silently move the zero by several microvolts while
// the status report still claimed the unit was calibrated.  At David's
// 20 uV minimum the alarm band is only +/-2 uV, so a silent ~5.9 uV step
// would throw alarms for the rest of the run and the operator would have no
// way of knowing why.
void invalidateSystemZeroCalibration() {
  bool had = sysocalDone;
  if (had) { sysocalDone = false; sysocalLost = true; }

  // Voiding the scan does not depend on 'had'.  Any re-initialisation
  // mid-scan changes the zero under a baseline that was learned with the
  // old one, so the recording cannot be trusted whatever the flags said.
  // The caller re-calibrates afterwards and the operator must press START
  // again: silently recalibrating and carrying on would leave one CSV
  // containing two different instruments.
  if (recording()) {
    Serial.println(F("*** ADS1256 re-initialised DURING A SCAN - this scan is VOID. ***"));
    scanStatus = SCAN_ADC_FAULT;
    actionStopScan();
  } else if (had) {
    Serial.println(F("*** ADS1256 re-initialised - zero calibration no longer valid. ***"));
  }
}

// A refusal that happens AFTER the SYSOCAL command has been issued must
// ALSO drop the calibration it replaced.  By that point the ADS1256's
// offset registers have already been rewritten, so the previous zero no
// longer describes the hardware - and sysocalDone is what lets a scan
// start and what stamps "zero_calibrated,yes" into every CSV header.
// Leaving the old flags standing would let the instrument scan on an
// offset it had just explicitly refused to certify, while the status
// report and the file both swore it was verified.
// Refusals BEFORE the SYSOCAL command (the sanity check, a relay that
// never produced a zero) are different: nothing was written, so the
// previous calibration is still true and is correctly left alone.
bool calibrationRefused() {
  sysocalDone = false;
  sysocalLost = true;
  soundStart(SND_ERROR);
  return false;
}

// The low-level calibration.  It assumes the input is ALREADY shorted and
// settled, and it does not touch the relay - automaticZeroCalibration()
// below owns that, so no caller can forget to release it.  Everything is
// printed, including the zero before and after and the chip's own
// offset-calibration registers, so a bench result can be pasted straight
// into the test log.
bool doZeroCalibration() {
#if TEST_MODE
  Serial.println(F("(Test mode: there is no real ADC to calibrate.)"));
  return false;
#else
  long  ofcBefore  = adsReadOffsetCalibration();
  float zeroBefore = 0.0;
  bool  haveBefore = adsAverageBlocking(SYSOCAL_AVG_POINTS, zeroBefore);
  Serial.print(F("Before: "));
  if (haveBefore) printMv(Serial, zeroBefore); else Serial.print(F("?"));
  Serial.print(F(" mV  OFC=")); Serial.println(ofcBefore);

  // Guard against calibrating an input that is not actually shorted.
  // SYSOCAL takes whatever is on the inputs to be zero, so if the relay
  // failed to close - stuck contact, open coil, broken wire, dead
  // transistor - this would quietly cancel the very signal the unit is
  // meant to measure, and every later reading would be wrong by that
  // amount with nothing to show for it.  A genuine shorted input sits
  // within a few microvolts of zero; the 21 Sep bench figure was 2.53 uV
  // against this 10 uV limit, so a healthy relay passes easily.
  // Note the "!haveBefore ||".  Up to v2.1 this read "haveBefore &&", so a
  // single lost conversion inside the 10-sample average disabled the guard
  // completely and SYSOCAL ran unchecked.  That was survivable while this
  // was a manual bench command with a human at the switch; it is not
  // survivable now that the same code runs unattended at every power-up
  // and after every background ADC recovery.  No measurement = no consent.
  if (!haveBefore || fabs(zeroBefore) > CAL_SANITY_MV) {
    Serial.println(F("*** Relay did not short the input - calibration SKIPPED. ***"));
    Serial.println(F("Check the relay, its driver and the CENTER/SHELL wiring."));
    soundStart(SND_ERROR);
    return false;
  }

  Serial.println(F("Calibrating..."));
  // From here on the chip's offset registers may already have changed,
  // so every exit below goes through calibrationRefused().
  if (!adsSystemOffsetCalibrate()) {
    Serial.println(F("*** FAILED - no DRDY. Power-cycle, check the DRDY wire. ***"));
    return calibrationRefused();
  }

  long  ofcAfter  = adsReadOffsetCalibration();
  float zeroAfter = 0.0;
  bool  haveAfter = adsAverageBlocking(SYSOCAL_AVG_POINTS, zeroAfter);
  Serial.print(F("After:  "));
  if (haveAfter) printMv(Serial, zeroAfter); else Serial.print(F("?"));
  Serial.print(F(" mV  OFC=")); Serial.println(ofcAfter);

  // Two ways SYSOCAL can return without having taken effect.  v2.1 only
  // printed a note about them, which was reasonable when a human was
  // reading the Serial Monitor at the bench.  Now sysocalDone is what
  // lets a scan start, so both are refusals: the instrument must never
  // claim a zero it cannot show evidence for.
  if (!haveAfter) {
    Serial.println(F("*** Could not verify the new zero - calibration REFUSED. ***"));
    return calibrationRefused();
  }
  if (fabs(zeroAfter) > CAL_VERIFY_MV) {
    Serial.print(F("*** Residual zero exceeds ")); printMv(Serial, CAL_VERIFY_MV);
    Serial.println(F(" mV - calibration REFUSED. ***"));
    return calibrationRefused();
  }
  Serial.print(F("Removed: ")); printMv(Serial, zeroBefore - zeroAfter);
  Serial.println(F(" mV"));
  sysocalZeroBefore = zeroBefore;
  sysocalZeroAfter  = zeroAfter;
  sysocalCount++;
  sysocalDone = true;
  sysocalLost = false;
  soundStart(SND_CAL_OK);
  return true;
#endif
}

// Blocking settle, used while the relay is closed and nothing else has
// to run.  Keeps the watchdog fed, lets any feedback sound finish rather
// than sticking on one note, and blinks the green LED so the operator can
// see the unit is busy calibrating.
void calSettle(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    watchdogKick();
    updateBuzzer();
    digitalWrite(PIN_LED_GREEN, ((millis() % 600) < 300) ? HIGH : LOW);
    delay(5);
  }
}

// ---------------------------------------------------------------------
//  AUTOMATIC ZERO CALIBRATION  -  the relay-controlled wrapper
// ---------------------------------------------------------------------
// This is the only function in the sketch that closes the relay, and it
// is written with ONE exit so that the relay is released on every path -
// success, failed sanity check, SYSOCAL timeout or lost DRDY.  That
// matters more than it looks: a relay left closed shorts CENTER to SHELL
// and the instrument would read a flat ~0 uV for ever, with a green LED,
// no error, and a survey full of plausible-looking nothing.
//
// A watchdog reset during the settle is covered too - the 13 k base
// pull-down holds the transistor off while the MCU is in reset, and
// setup() drives D3 LOW before it does anything else.
//
//   close relay  ->  discard settling conversions  ->  settle
//     ->  measure shorted zero  ->  sanity-check it  ->  SYSOCAL
//     ->  verify the zero  ->  OPEN RELAY  ->  discard settling
//
bool automaticZeroCalibration() {
#if TEST_MODE
  // No real converter to calibrate, but the workflow downstream (START
  // gating, status, CSV header) must still be testable.
  Serial.println(F("(Test mode: zero calibration simulated.)"));
  sysocalCount++;
  sysocalDone = true;
  sysocalLost = false;
  return true;
#else
  if (!adcOk) {
    Serial.println(F("(ADS1256 not responding - zero calibration not possible.)"));
    soundStart(SND_ERROR);
    return false;
  }

  bool  ok         = false;
  Phase savedPhase = phase;
  phase = CALIBRATING;          // blinking green, and no "not calibrated" fault wink

  Serial.println(F("---- ZERO CALIBRATION ----"));
  relayClose();
  Serial.println(F("Relay CLOSED. Settling..."));

  if (!adsDiscardConversions(RELAY_SETTLE_CONVERSIONS)) {
    Serial.println(F("*** No DRDY after closing the relay. ***"));
    soundStart(SND_ERROR);
  } else {
    calSettle(CAL_SETTLE_MS);
    adsDiscardConversions(RELAY_SETTLE_CONVERSIONS);   // the settle read nothing
    ok = doZeroCalibration();
  }

  // ---- single release point: reached however the block above ended ----
  relayOpen();
  adsDiscardConversions(RELAY_SETTLE_CONVERSIONS);

  // Show what the input reads once the relay has released.  A welded reed
  // contact is the one failure with no other symptom - the unit would
  // read a flat ~0 uV for ever behind a solid green LED - and firmware
  // cannot tell a stuck relay from a genuinely quiet antenna, so this
  // reports the number rather than judging it.  (Up to v2.4 the learning
  // period then aborted any scan whose baseline came out near zero; v2.5
  // removed that so genuinely tiny baselines can be scanned, which leaves
  // this number and a near-zero baseline in the CSV as the signs of a
  // welded relay.)
  float openMv;
  Serial.print(F("Relay OPEN, input "));
  if (adsAverageBlocking(SYSOCAL_AVG_POINTS, openMv)) printMv(Serial, openMv);
  else Serial.print(F("?"));
  Serial.println(F(" mV"));

  // The blocking reads above left a long gap since the last conversion,
  // which updateAdcHealth() would otherwise read as a dead converter.
  lastConversionMs = millis();
  drdyArmed = false;
  lastIdlePrintMs  = millis();
  phase = savedPhase;
  return ok;
#endif
}

// Serial command 'c'.  The operator types c and touches nothing: the
// relay shorts the input, SYSOCAL runs, the relay releases.
void actionSystemZeroCalibrate() {
  if (recording()) {
    Serial.println(F("(Press STOP first, then type c.)"));
    soundStart(SND_ERROR);
    return;
  }
  if (automaticZeroCalibration()) {
    Serial.println(F("Zero calibration complete. Press START when ready."));
  }
}

// Power-on sequence.  The relay was driven OPEN as the first act of
// setup(), adsInitialise() has already reset, configured and SELFCAL'd
// the converter with the antenna connected, and this now adds the
// relay-driven SYSOCAL that removes the offset of the whole signal chain.
//
// It deliberately does NOT learn a ground baseline.  Electrical zero
// calibration and soil-baseline learning are two different things: the
// baseline comes from the first 5 s after START, with the antenna in its
// normal scanning position over representative ordinary ground.
void runStartupCalibration() {
  phase = CALIBRATING;
  automaticZeroCalibration();
  phase         = READY;
  baselineReady = false;        // no ground baseline until START is pressed

  if (sysocalDone) {
    Serial.println(F("READY. Hold the antenna over ordinary ground, then press START."));
  } else {
    Serial.println(F("NOT READY - zero calibration failed. START will retry it."));
    soundStart(SND_ERROR);
  }
}

// =====================================================================
//  SETUP
// =====================================================================
void setup() {
  // Optiboot clears MCUSR before the sketch starts, so the application
  // cannot reliably report a hardware reset cause on a standard Uno.
  // A recording without a complete [SUMMARY] is the durable evidence of
  // any reset or power loss that interrupted a scan.
  MCUSR = 0;
  wdt_disable();

  // THE RELAY COMES FIRST.  Until D3 is a driven output the instrument's
  // input could in principle be shorted, and everything downstream - the
  // start-up SELFCAL, the first idle readings, the sanity check - would
  // be measuring a short instead of the antenna.  The 13 k base pull-down
  // holds the transistor off through boot and reset; this makes it
  // deliberate rather than merely likely.
  pinMode(PIN_ZERO_RELAY, OUTPUT);
  relayOpen();

  // Buzzer next: a low-trigger module would otherwise sound while the pin floats
  pinMode(PIN_BUZZER, OUTPUT);
  applyTone(0);
  digitalWrite(PIN_BUZZER, BUZZER_IDLE_LEVEL);

  // Chip-select lines HIGH so the two SPI devices stay quiet
  pinMode(PIN_ADC_CS, OUTPUT);  digitalWrite(PIN_ADC_CS, HIGH);
  pinMode(PIN_SD_CS,  OUTPUT);  digitalWrite(PIN_SD_CS,  HIGH);
  pinMode(PIN_ADC_DRDY, INPUT_PULLUP);   // a missing DRDY wire then reads "no data" instead of floating
  pinMode(PIN_ADC_PDWN, OUTPUT); digitalWrite(PIN_ADC_PDWN, HIGH);
  // No reset pin: this ADS1256 module does not have one.  adsInitialise()
  // uses the software RESET command (0xFE) instead.

  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP,  INPUT_PULLUP);
  pinMode(PIN_BTN_THRESHOLD, INPUT_PULLUP);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED,   OUTPUT);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED,   LOW);

  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  // One title line: printVersion() straight after it gives the version,
  // so repeating it between two rules of '=' only cost flash (v2.8).
  Serial.println(F("==== Millivolt Measurement & Alarm ===="));
  printVersion();
  // The settings all go into every CSV header now, so the banner only
  // states the rule the operator is about to rely on.
  Serial.print(F("Alarm: baseline +/- ")); Serial.print(selectedAlarmPercent(), 1);
  Serial.print(F(" %, ")); Serial.print(ALARM_BOTH_DIRECTIONS ? F("either way") : F("above only"));
  Serial.print(F(", ")); Serial.print(ALARM_CONFIRM_POINTS); Serial.println(F(" to confirm"));
#if TEST_MODE
  Serial.println(F("*** TEST MODE: the ADC is simulated (about 0.072 mV with noise). ***"));
#endif

  // Power-on self test: both LEDs and a short rising jingle
  digitalWrite(PIN_LED_GREEN, HIGH); digitalWrite(PIN_LED_RED, HIGH);
  soundPlayBlocking(SND_POWER_ON);
  digitalWrite(PIN_LED_GREEN, LOW);  digitalWrite(PIN_LED_RED, LOW);

#if ENABLE_WATCHDOG
  wdt_enable(WDTO_8S);       // generous: a slow card write must never trip it
#endif

  SPI.begin();

  // ---- SD card ----
  if (sdStart()) {
    Serial.println(F("SD OK; files are SCANxxxx.CSV."));
  } else {
    Serial.println(F("SD missing. Switch OFF before fitting one."));
  }
  watchdogKick();

  // ---- ADC ----
#if TEST_MODE
  adcOk = true;
#else
  adcOk = adsInitialise(false);
  if (adcOk) {
    Serial.println(F("ADS1256 OK: PGA64, 10 SPS, buffer on."));
  } else {
    Serial.println(F("ADS1256 FAIL: check power and SPI wiring."));
    soundStart(SND_ERROR);
  }
  adcRetryMs = millis();
#endif

  printHelp();
  runStartupCalibration();
  lastConversionMs = millis();
}

// =====================================================================
//  ONE ADC CONVERSION  ->  ONE POINT
// =====================================================================
// There is no sample clock any more.  Every fresh ADS1256 conversion
// becomes exactly one point, stamped with the millisecond it was really
// read.  Two things follow.  A logged point always corresponds to a
// genuine conversion - never an average of five, never an empty slot.
// And the converter's exact rate stops mattering: 10 SPS from a crystal
// half a percent slow simply produces points half a percent further
// apart, instead of leaving one scheduled slot in two hundred empty.
bool adcPoll(float &mv) {
#if TEST_MODE
  static unsigned long lastFakeMs = 0;
  static float anomalyLevel = 0.0;
  if (millis() - lastFakeMs < ADC_NOMINAL_MS) return false;
  lastFakeMs = millis();
  float drift = 0.001 * sin(millis() / 30000.0);
  float noise = random(-8, 9) / 10000.0;
  float anomalyTarget = 0.0;
  if (phase == SCANNING) {
    if      (simulatedAnomalyLevel == 1) anomalyTarget = 0.072 * 0.20;
    else if (simulatedAnomalyLevel == 2) anomalyTarget = 0.072 * 0.60;
    else if (simulatedAnomalyLevel == 3) anomalyTarget = 0.072 * 1.20;
  }
  anomalyLevel += (anomalyTarget - anomalyLevel) * 0.15;
  mv = 0.072 + drift + noise + anomalyLevel;
  lastConversionMs = millis();
  return true;
#else
  if (!adcOk) return false;
  // One conversion requires one complete DRDY HIGH -> LOW cycle.  Without
  // this gate a DRDY wire shorted to ground would be mistaken for fresh
  // data and the same result register could be logged at loop speed.
  if (digitalRead(PIN_ADC_DRDY) != LOW) {
    drdyArmed = true;
    return false;
  }
  if (!drdyArmed) return false;
  drdyArmed = false;

  unsigned long now  = millis();
  unsigned long gap  = now - lastConversionMs;
  long counts = adsReadConversion();
  // RDATA raises DRDY after the 24 data bits.  Re-arm here so a slow SD
  // write cannot hide the brief HIGH interval before the next conversion.
  // A wire held LOW is an immediate protocol fault; its count is rejected.
  drdyArmed = (digitalRead(PIN_ADC_DRDY) == HIGH);
  if (!drdyArmed) {
    adcOk = false;
    adcRetryMs = millis();
    Serial.println(F("*** DRDY stayed LOW after RDATA - ADC fault. ***"));
    soundStart(SND_ERROR);
    if (recording()) {
      scanStatus = SCAN_ADC_FAULT;
      actionStopScan();
    }
    return false;
  }
  lastConversionMs = now;

  // If the program was away for longer than one conversion period, the
  // result register may have been updating as we read it, and whatever
  // conversions fell in the gap are gone for good.  Count them, discard
  // this one, and take the next - which is guaranteed clean, because
  // reading has just reset DRDY.  This is the v1.3 stall protection,
  // now costing one conversion after a stall instead of a whole point.
  if (gap > ADC_MISS_MS) {
    unsigned long missed = gap / ADC_NOMINAL_MS;
    if (missed > 100) missed = 1;              // first reading after power-up
    if (recording()) droppedPoints += missed;
    droppedTotal += missed;
    return false;
  }

  if (counts >= ADC_SATURATED_COUNTS || counts <= -ADC_SATURATED_COUNTS) {
    // The input is at the end stop: +/-78 mV differential at PGA 64.
    // Almost always an unplugged antenna or a wiring fault.  A saturated
    // value is NEVER allowed into baseline learning or scan statistics.
    if (saturationRun == 0) {
      Serial.println(F("WARNING: ADC input at full scale - check the antenna and the bias point."));
    }
    if (saturationRun < 255) saturationRun++;
    saturatedTotal++;
    if (recording()) saturatedPoints++;
    if (!inputFault && saturationRun >= ADC_SATURATION_CONFIRM_POINTS) {
      inputFault = true;
      Serial.println(F("*** ADC input fault confirmed - full-scale readings persisted. ***"));
      soundStart(SND_ERROR);
      if (recording()) {
        scanStatus = SCAN_INPUT_FAULT;
        actionStopScan();
      }
    }
    return false;
  }
  saturationRun = 0;
  inputFault = false;
  mv = adsCountsToMillivolts(counts);
  return true;
#endif
}

// Keep an eye on the converter and bring it back by itself if it drops out.
void updateAdcHealth() {
#if !TEST_MODE
  if (adcOk) {
    // Conversions arrive every ADC_NOMINAL_MS.  A long silence means the
    // converter has stopped talking - this replaces the old missed-point
    // counter, which no longer exists now that points are event driven.
    if ((millis() - lastConversionMs) > ADC_SILENT_MS) {
      adcOk = false;
      adcRetryMs = millis();
      Serial.println(F("*** ADS1256 stopped sending conversions - re-checking it. ***"));
      soundStart(SND_ERROR);
      // Void immediately.  Waiting for the later recovery attempt left a
      // window in which STOP could incorrectly label the file COMPLETE.
      if (recording()) {
        scanStatus = SCAN_ADC_FAULT;
        actionStopScan();
      }
    }
    return;
  }
  // Re-initialising takes about a tenth of a second, and longer if the
  // chip answers but never signals ready.  While a recording is running
  // that time is better spent keeping the buttons and the Serial Monitor
  // responsive, so try less often then.
  unsigned long wait = recording() ? ADC_RETRY_BUSY_MS : ADC_RETRY_MS;
  if ((millis() - adcRetryMs) < wait) return;
  adcRetryMs = millis();
  if (adsInitialise(true)) {
    adcOk = true;
    adcResets++;
    Serial.println(F("ADS1256 is answering again."));
    // The re-initialisation destroyed the SYSOCAL (RESET and SELFCAL both
    // rewrite the offset registers), so the converter being healthy is not
    // the same as the instrument being ready.  Earn the zero back before
    // calling it measurement-ready.  Any scan was already voided and
    // stopped by invalidateSystemZeroCalibration(), so blocking here for
    // the settle is safe, and the operator must press START again.
    automaticZeroCalibration();
    if (sysocalDone) {
      Serial.println(F("Measurement restored."));
      soundStart(SND_CLICK);
    } else {
      // automaticZeroCalibration() already started the error pattern.  Do
      // not overwrite it with a success-like click after a failed recovery.
      Serial.println(F("START will retry zero."));
    }
  }
#endif
}

// =====================================================================
//  SERIAL COMMANDS  (so the unit can be driven from the Serial Monitor)
// =====================================================================
void handleSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '1': case '3': actionStartScan(); break;
      case '2': case '4': actionStopScan();  break;
      case 's': case 'S': printStatus(); break;
      case 'v': case 'V': printVersion(); break;
      case 'r': case 'R':
#if TEST_MODE
        Serial.println(F("(Test mode: there is no real ADC to re-check.)"));
#else
        if (recording()) {
          Serial.println(F("(Press STOP first.)"));
        } else {
          Serial.println(F("Re-starting the ADS1256..."));
          adcOk = adsInitialise(false);
          Serial.println(adcOk ? F("ADS1256 OK.") : F("Still not responding."));
          // The re-initialisation wiped the SYSOCAL, so earn it back here
          // rather than leaving the operator to remember to type c.  Same
          // rule as the start-up and background-recovery paths.
          if (adcOk) { adcResets++; automaticZeroCalibration(); }
        }
#endif
        break;
      case 'c': case 'C': actionSystemZeroCalibrate(); break;
      case '?': case 'h': case 'H': printHelp(); break;
#if TEST_MODE
      case 'a': case 'A':
        simulatedAnomalyLevel = (uint8_t)((simulatedAnomalyLevel + 1) % 4);
        Serial.print(F("Simulated anomaly: "));
        if      (simulatedAnomalyLevel == 1) Serial.println(F("+20 %"));
        else if (simulatedAnomalyLevel == 2) Serial.println(F("+60 %"));
        else if (simulatedAnomalyLevel == 3) Serial.println(F("+120 %"));
        else                                 Serial.println(F("OFF"));
        break;
#endif
      default: break;   // ignore newlines and anything else
    }
  }
}

// =====================================================================
//  WHAT TO DO WITH ONE POINT
// =====================================================================
void handlePoint(float mv) {
  unsigned long now = millis();
  lastReadingMv = mv;

  if (!recording()) {
    // READY or CALIBRATING: a live reading every 2 s, so the bench
    // operator can see the unit working without starting a scan.
    if (now - lastIdlePrintMs >= IDLE_PRINT_MS) {
      lastIdlePrintMs = now;
      Serial.print(F("idle  live reading = ")); printMv(Serial, mv); Serial.println(F(" mV"));
    }
    return;
  }

  sampleNumber++;
  unsigned long tms = now - phaseStartMs;

  // Retire anything that has fallen out of the 30 s window.  Done on
  // every point, accepted or not, so the window keeps moving even while
  // the operator is standing over an anomaly.
  baselineAgeOut(now);

  // ---- SETTLING: the first SETTLE_MS after START ----------------------
  // Logged, numbered and timestamped like any other reading, but it
  // enters nothing: not the baseline, not the statistics, not a count
  // that claims it was judged.  The START tone is still sounding for a
  // quarter of this period.
  if (phase == SETTLING) {
    settlingPoints++;
    logScanRow(sampleNumber, tms, mv, 0.0, 0.0, PH_SETTLE, false, 0, false);
    if (tms >= SETTLE_MS) phase = LEARNING;
    return;
  }

  // ---- LEARNING: the next INITIAL_LEARNING_MS -------------------------
  // These readings build the baseline, so they go into baseStats - which
  // is what tells the operator afterwards whether the antenna was held
  // still while the instrument decided what "normal" meant.
  if (phase == LEARNING) {
    baselineAccept(mv);
    statsAdd(baseStats, mv);
    learningPoints++;
    logScanRow(sampleNumber, tms, mv, baselineMv, 0.0, PH_LEARN, false, 0, false);

    if (tms >= SETTLE_MS + INITIAL_LEARNING_MS) {
      baselineReady = true;
      initialBaselineMv = baselineMv;   // frozen, for the summary
      Serial.print(F("---- BASELINE ")); printMv(Serial, baselineMv);
      Serial.print(F(" mV from ")); Serial.print(learningPoints);
      Serial.println(F(" readings ----"));
      printAlarmRule(scanPercent);
      // Tell the operator that baseline learning is complete, then let the
      // ADC recover before the actual scan begins.  This short click and
      // the three recovery conversions happen before SCANNING, so no
      // active-scan points are discarded or corrupted by the notification.
      soundPlayBlocking(SND_CLICK);
#if !TEST_MODE
      if (!adsDiscardConversions(3)) {
        adcOk = false;
        scanStatus = SCAN_ADC_FAULT;
        actionStopScan();
        return;
      }
      drdyArmed = false;
      lastConversionMs = millis();
#endif
      phase = SCANNING;
    }
    return;
  }

  // ---- SCANNING --------------------------------------------------------
  // Order of operations matters, and is the heart of the client's spec:
  // judge the point against the baseline AS IT STANDS, and only then
  // decide whether the point is allowed to change it.  A reading that
  // looks abnormal is logged and counted, but never averaged in - so a
  // real anomaly can never drag the baseline towards itself and end up
  // being treated as the new normal.
  statsAdd(scanStats, mv);

  float usedBase = baselineMv;
  float thr, low, high;
  alarmLimits(usedBase, scanPercent, thr, low, high);

  // NORMAL is  low <= reading <= high,  so a reading exactly on a limit
  // counts as normal.
  bool abnormal = ALARM_BOTH_DIRECTIONS ? (mv < low || mv > high) : (mv > high);

  bool alarm;
  if (!abnormal) {
    abnormalRun = 0;
    abnormalSide = 0;
    alarm       = false;
    scanNormal++;
    baselineAccept(mv);                  // accepted: it enters the window
  } else {
    // A real sustained event remains on one side of the baseline.  The
    // measured SD-write disturbance is a short high-then-low pair, so an
    // opposite-side point starts a new candidate instead of confirming it.
    int8_t side = (mv > high) ? 1 : -1;
    if (side != abnormalSide) {
      abnormalSide = side;
      abnormalRun = 1;
    } else if (abnormalRun < 255) {
      abnormalRun++;
    }
    scanAbnormal++;
    alarm = (abnormalRun >= ALARM_CONFIRM_POINTS);
    // The window is deliberately not touched here.
  }

  if (alarm && !alarmActive) { alarmStartedMs = millis(); alarmEvents++; }
  alarmActive = alarm;
  if (alarm) {
    alarmPoints++;
    if (!firstAlarmPoint) firstAlarmPoint = sampleNumber;
    lastAlarmPoint = sampleNumber;
  }
  float delta = mv - usedBase;
  if (scanStats.n == 1 || fabs(delta) > maxAbsDelta) {
    maxAbsDelta   = fabs(delta);
    maxDeltaPoint = sampleNumber;   // a size with no location cannot be looked up
    maxDeltaPctValid = (fabs(usedBase) > 1e-9);
    if (maxDeltaPctValid) maxDeltaPct = (fabs(delta) / fabs(usedBase)) * 100.0;
  }

  logScanRow(sampleNumber, tms, mv, usedBase, thr, PH_SCAN, abnormal, abnormalRun, alarm);
}

// =====================================================================
//  MAIN LOOP
// =====================================================================
void loop() {
  watchdogKick();

  // Acquisition first, and the SD write immediately after it.  Starting
  // a write just after a conversion has been read leaves the maximum
  // slack - about 95 ms at 10 SPS - before the next one is due.  That
  // ordering, plus flushing on a timer rather than per row, is what
  // keeps the card from costing us conversions.  A queue of pending
  // rows would not help: if the program is inside a blocking write when
  // DRDY falls, the ADS1256 overwrites that result either way.
  float mv;
  if (adcPoll(mv)) {
    handlePoint(mv);
    logFlushIfDue();
  }

  updateAdcHealth();
  updateBuzzer();
  updateLeds();
  handleSerialCommands();

  // ---- buttons ----
  if (buttonWasPressed(btnThreshold)) actionCycleThreshold();
  if (buttonWasPressed(btnStart)) actionStartScan();
  if (buttonWasPressed(btnStop))  actionStopScan();
}
