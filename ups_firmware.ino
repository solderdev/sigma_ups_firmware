/*!
 * @file  fourBatteriesLPUPS.ino
 * @brief LPUPS reports battery information to the computer via USB-HID
 * @details Reads battery information from UPS via I2C, and reports this information to the computer via USB-HID
 * @copyright  Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license  The MIT License (MIT)
 * @author  [qsjhyy](yihuan.huang@dfrobot.com)
 * @version  V1.1
 * @date  2023-08-09
 * @url  https://github.com/DFRobot/DFRobot_LPUPS
 */
#include <DFRobot_LPUPS.h>
#include <HIDPowerDevice.h>
#include "upsDef.h"

DFRobot_LPUPS_I2C LPUPS(&Wire, /*I2CAddr*/ UPS_I2C_ADDRESS);

uint16_t iPreviousStatus = 0;   // Now and previous device status.
byte iRemaining = 0, iPrevRemaining = 100;
int iRes = 0;
uint16_t iPrevRunTimeToEmpty = 0;
uint16_t iPrevVoltage = 0;

bool bCommsLost = false;   // No report delivered to the host for COMMS_LOST_TIMEOUT_MS
// millis() of the last report that entered a free endpoint bank. The endpoint
// is double-banked, so this proves the host was draining it recently, not
// that it saw this very report; see COMMS_LOST_TIMEOUT_MS in upsDef.h.
unsigned long lastReportOkMs = 0;
bool bRetrySend = false;   // Last burst delivered nothing; retry next loop

// Active LED pattern, set by updateLeds() for serial debugging. Kept in flash
// via F(): RAM is nearly full, so these strings must not land in .data.
const __FlashStringHelper* ledPattern;

// Host power-on state (see updateHostPowerOn). All millis() comparisons use
// unsigned subtraction; a host off for >49.7 days can delay a press by up to
// one PWRON_LED_OFF_MS window when lastLedOnMs re-wraps — cosmetic, unhandled.
uint16_t hostLedAdc = 0;          // Last raw PWR_LED sense reading, for calibration
bool bHostLedOn = false;          // Debounced host power LED state
unsigned long lastLedOnMs = 0;    // millis() of the last debounced "LED on" reading
bool bPwrOnArmed = false;         // Host observed down during/just after an AC outage
bool bAcBack = false;             // AC present again after an outage seen since boot
unsigned long acBackSinceMs = 0;  // millis() when AC returned; valid while bAcBack
uint8_t pwrOnTries = 0;           // Presses this outage cycle
unsigned long lastPressMs = 0;    // millis() of the last press; valid when pwrOnTries > 0
const __FlashStringHelper* pwrOnState;   // For serial debugging, like ledPattern

// Rolling average of the measured pack voltage (see VBAT_SMOOTH_SAMPLES)
uint16_t vbatSamples[VBAT_SMOOTH_SAMPLES];
uint32_t vbatSum = 0;   // 16 samples of up to 19200 mV exceed uint16_t
uint8_t vbatIndex = 0;
bool bVbatFilled = false;   // Buffer has been prefilled with a valid sample
uint16_t smoothedVoltage = 0;   // Averaged pack voltage in mV, 0 until filled


void setup(void)
{
  // Initialize UPS indicator LEDs first, so they stay dark through the boot delay.
  // Writing HIGH before pinMode keeps the active-low LEDs off with no on-glitch.
  digitalWrite(UPS_GREEN_LED, HIGH);
  digitalWrite(UPS_RED_LED, HIGH);
  digitalWrite(UPS_BLUE_LED, HIGH);
  ledPattern = F("off");
  pinMode(UPS_GREEN_LED, OUTPUT);
  pinMode(UPS_RED_LED, OUTPUT);
  pinMode(UPS_BLUE_LED, OUTPUT);

  // Power-button line: high-Z with the output register held LOW, so a later
  // pinMode(OUTPUT) alone presses and pinMode(INPUT) releases, never passing
  // through a pull-up-enabled state.
  pinMode(HOST_PWR_BTN_PIN, INPUT);
  digitalWrite(HOST_PWR_BTN_PIN, LOW);
  pwrOnState = F("idle");

  delay(5000);
  Serial.begin(115200);
  Serial.println("Serial Begin"); //Avoid serial port not working

  // Init the sensor
  while (NO_ERR != LPUPS.begin(FOUR_BATTERIES_UPS_PID)) {
    Serial.println("Communication with device failed, please check connection");
    delay(3000);
  }
  Serial.println("Begin ok!");

  /**
   * @fn setMaxChargeVoltage
   * @brief Set the maximum charging voltage
   * @param data Maximum charging voltage:
   * @n          Three batteries: 11100 ~ 12600 mV
   * @n          Four batteries: 14800 ~ 16800 mV
   * @return None
   */
   maxChargeVoltage = 15600;
   LPUPS.setMaxChargeVoltage(maxChargeVoltage);

  // Wait for a first valid chip read (VBAT ADC nonzero) before going online
  // on USB-HID. The Arduino is routinely reflashed while the host is up: NUT
  // reconnects within seconds, and reporting the zeroed power-up state (no AC
  // present, 0 % charge) would read as "on battery, battery low" and force a
  // host shutdown. Until the features are registered, HID feature requests
  // stall and NUT just retries.
  LPUPS.getChipData(regBuf);
  printChargeData();
  while (!batteryVoltage) {
    Serial.println("VBAT ADC reads 0, waiting for valid chip data");
    delay(1000);
    LPUPS.getChipData(regBuf);
    printChargeData();
  }
  updateBatteryState();

  // Initialize HIDPowerDevice
  initPowerDevice();

  // Boot grace: anchor the comms-lost clock now so the host driver's restart
  // after a reflash (~11 s via the udev rule) never shows a spurious blue phase.
  lastReportOkMs = millis();
  // Same for the power-LED-off clock: a reflash must not start with the host
  // already looking "long off".
  lastLedOnMs = millis();
}


void loop()
{
  /************ Get charge chip data and print ****************************/
  /**
   * Get chip data
   * regBuf - data buffer for storing data
   */
  LPUPS.getChipData(regBuf);
  printChargeData();

  updateBatteryState();

  /************ Delay ***************************************/
  delayWithLeds(2000);

  /************ Batch send or send on change ***********************/
  // Voltage uses a ±1 cV deadband: residual averaging jitter can still toggle
  // the last centivolt digit, and the keepalive guarantees periodic sends.
  if ((iPresentStatus != iPreviousStatus) || (iRemaining != iPrevRemaining) ||
    (iRunTimeToEmpty != iPrevRunTimeToEmpty) ||
    (abs((int16_t)iVoltage - (int16_t)iPrevVoltage) > 1) ||
    bRetrySend ||
    (millis() - lastReportOkMs > COMMS_KEEPALIVE_MS)) {

    // 12 INPUT OR FEATURE(required by Windows)
    // Success of any attempted send means a bank was free, i.e. the host has
    // been draining the endpoint; the skipped RUNTIMETOEMPTY send must not
    // count as a failure.
    bool anyOk = false;
    anyOk |= (PowerDevice.sendReport(HID_PD_REMAININGCAPACITY, &iRemaining, sizeof(iRemaining)) >= 0);
    if (bDischarging) anyOk |= (PowerDevice.sendReport(HID_PD_RUNTIMETOEMPTY, &iRunTimeToEmpty, sizeof(iRunTimeToEmpty)) >= 0);
    anyOk |= (PowerDevice.sendReport(HID_PD_VOLTAGE, &iVoltage, sizeof(iVoltage)) >= 0);
    iRes = PowerDevice.sendReport(HID_PD_PRESENTSTATUS, &iPresentStatus, sizeof(iPresentStatus));
    anyOk |= (iRes >= 0);

    if (anyOk) lastReportOkMs = millis();
    bRetrySend = !anyOk;   // Fully failed burst: retry next loop instead of waiting for the keepalive

    // Consume the status change only if its own send was accepted; otherwise
    // the difference persists and the burst retries next loop instead of
    // deferring the change to the next keepalive.
    if (iRes >= 0) iPreviousStatus = iPresentStatus;
    iPrevRemaining = iRemaining; // Save new battery remaining capacity
    iPrevRunTimeToEmpty = iRunTimeToEmpty; // Save new estimated battery runtime count
    iPrevVoltage = iVoltage; // Save new reported battery voltage

  }

  // Comms lost = nothing accepted for COMMS_LOST_TIMEOUT_MS. Individual send
  // failures are routine (250 ms bank timeout vs the host's ~2 s poll cycle);
  // unsigned arithmetic keeps this millis()-wrap safe.
  bCommsLost = (millis() - lastReportOkMs > COMMS_LOST_TIMEOUT_MS);

  updateHostPowerOn();

  /************ Serial print reported battery level and operation result ******************/
  Serial.print("iRemaining = "); // Battery remaining capacity percentage
  Serial.println(iRemaining);
  Serial.print("iRunTimeToEmpty = "); // Estimated time to empty battery
  Serial.println(iRunTimeToEmpty);
  Serial.print("iRes = "); // Last PRESENTSTATUS send result; a lone -1 is normal (endpoint bank busy)
  Serial.println(iRes);
  Serial.print(F("lastOk age = ")); // Seconds since the host last accepted a report
  Serial.println((millis() - lastReportOkMs) / 1000);
  Serial.print(F("LEDs = ")); // Active LED pattern chosen by updateLeds()
  Serial.println(ledPattern);
  Serial.print(F("host LED ADC = ")); // Raw PWR_LED sense value, for threshold calibration
  Serial.print(hostLedAdc);
  Serial.print(F(", off for s = ")); // Seconds since the last debounced "LED on"
  Serial.println((millis() - lastLedOnMs) / 1000);
  Serial.print(F("pwrOn = ")); // Host power-on state machine, tries used
  Serial.print(pwrOnState);
  Serial.print(F(", tries = "));
  Serial.println(pwrOnTries);
  Serial.println();
}

// Auto power-on of the host after an outage-induced shutdown. The UPS cannot
// cut host power, so after NUT halts the host on low battery the machine
// would stay off forever once AC returns; instead we pulse the front-panel
// power button. Arms only when the host is observed down (power LED off and
// USB comms lost) while AC is absent, or within a short grace window after AC
// returns (an FSD halt may finish just after the outage ends). A manual
// poweroff on mains therefore never triggers a press; one done on battery
// during an outage is indistinguishable from an FSD and will be rebooted.
void updateHostPowerOn(void)
{
  unsigned long now = millis();

  // Track AC returning after an outage observed since boot. Presence before
  // any outage never sets bAcBack, so arming is impossible until an outage
  // has actually been witnessed.
  static bool prevAcPresent = true;
  if (!bACPresent) {
    bAcBack = false;
  } else if (!prevAcPresent) {
    bAcBack = true;
    acBackSinceMs = now;
  }
  prevAcPresent = bACPresent;

  bool ledLongOff = (now - lastLedOnMs > PWRON_LED_OFF_MS);

  // Clear: host up on mains, whether the outage was survived or the press
  // worked. Ends the outage cycle.
  if (bHostLedOn && bACPresent) {
    bPwrOnArmed = false;
    pwrOnTries = 0;
    pwrOnState = F("idle");
    return;
  }

  // Arm: host seen down during the outage or within the post-outage grace.
  if (!bPwrOnArmed && ledLongOff && bCommsLost &&
      (!bACPresent || (bAcBack && now - acBackSinceMs < PWRON_ARM_GRACE_MS))) {
    // No serial print: arming implies the host is down, nobody is listening.
    // The state is visible later via pwrOnState in the periodic serial block.
    bPwrOnArmed = true;
    pwrOnTries = 0;
  }

  if (!bPwrOnArmed) return;

  if (!bACPresent || !bAcBack) {
    pwrOnState = F("armed, waiting for AC");
    return;
  }
  if (now - acBackSinceMs < PWRON_AC_STABLE_MS) {
    pwrOnState = F("armed, AC stabilizing");
    return;
  }
  if (pwrOnTries >= PWRON_MAX_TRIES) {
    pwrOnState = F("gave up (max tries)");
    return;
  }
  if (pwrOnTries > 0 && now - lastPressMs < PWRON_RETRY_MS) {
    pwrOnState = F("pressed, waiting for boot");
    return;
  }
  // Both vetoes must still hold at press time: a healthy USB link or a lit
  // power LED (e.g. broken sense wire on a running host) blocks the press.
  if (!ledLongOff || !bCommsLost) {
    pwrOnState = F("armed, veto");
    return;
  }

  // Pressing the host power button. No serial print: the host is down by
  // definition here; the press shows up as pwrOnTries in the periodic block.
  pinMode(HOST_PWR_BTN_PIN, OUTPUT);   // Output register is LOW: line pulled down
  delay(PWR_BTN_PULSE_MS);
  pinMode(HOST_PWR_BTN_PIN, INPUT);    // Release to high-Z, header pull-up recovers
  pwrOnTries++;
  lastPressMs = millis();
  pwrOnState = F("pressed, waiting for boot");
}

// Debounced host power LED sense, sampled at the delayWithLeds() 25 ms
// cadence so a blinking suspend LED (period well under 2 s) always refreshes
// lastLedOnMs and never looks like power-off. PWR_LED_ON_SAMPLES consecutive
// on-reads are required so a single noise spike coupled onto the high-Z
// 100k-pulled-down node cannot reset the off-timer.
void sampleHostPowerLed(void)
{
  static uint8_t consecOn = 0;
  hostLedAdc = analogRead(HOST_PWR_LED_ADC);
  if (hostLedAdc > PWR_LED_ON_ADC_MIN) {
    if (consecOn < PWR_LED_ON_SAMPLES) consecOn++;
    if (consecOn >= PWR_LED_ON_SAMPLES) {
      bHostLedOn = true;
      lastLedOnMs = millis();
    }
  } else {
    consecOn = 0;
    bHostLedOn = false;
  }
}

// Derive all USB-HID reported values from the last chip read: smoothed pack
// voltage, remaining capacity, AC/charging/discharging flags and status bits.
// Also called from setup() before the HID features are registered, so a host
// that is already up never sees the zeroed power-up state.
void updateBatteryState(void)
{
  // Feed the rolling voltage average. Skip samples where the VBAT ADC reads 0
  // (dead ADC; setup waits for a first valid read, and later I2C failures
  // leave regBuf stale rather than zero).
  if (batteryVoltage) {
    if (!bVbatFilled) {
      // Prefill with the first valid sample so boot doesn't report a low average
      for (uint8_t i = 0; i < VBAT_SMOOTH_SAMPLES; i++) {
        vbatSamples[i] = batteryVoltage;
      }
      vbatSum = (uint32_t)batteryVoltage * VBAT_SMOOTH_SAMPLES;
      bVbatFilled = true;
    } else {
      vbatSum -= vbatSamples[vbatIndex];
      vbatSamples[vbatIndex] = batteryVoltage;
      vbatSum += batteryVoltage;
      vbatIndex = (vbatIndex + 1) % VBAT_SMOOTH_SAMPLES;
    }
    smoothedVoltage = vbatSum / VBAT_SMOOTH_SAMPLES;

    // Report smoothed pack voltage in centivolts (HID descriptor unit)
    iVoltage = smoothedVoltage / 10;
  }

  /*********** Unit of measurement, measurement unit ****************************/
  /**
   * Battery voltage range: 12.3V ~ 16.8V, in order to keep the battery stable at extreme values:
   * Assuming the battery voltage range is 12.4V ~ 16.7V, corresponding to battery capacity 0 ~ 100.
   * Note: You can adjust the battery capacity more accurately by correcting the voltage mutation with dischargeCurrent if interested.
   */
  if (smoothedVoltage > MIN_BATTERY_VOLTAGE) {
    iRemaining = (((float)smoothedVoltage - MIN_BATTERY_VOLTAGE) / (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE)) * 100;
  } else if (bVbatFilled) {
    // The averaged voltage at/below the 0 % point is a genuine deep discharge,
    // not a transient — report empty.
    iRemaining = 0;
    Serial.println("The battery voltage is lower than normal !!!");   // Battery voltage lower than normal value.
  }

  if (100 < iRemaining) {
    iRemaining = 100;
  }

  // Please ensure to use the dedicated charger for LattePanda and connect it to the UPS (connect it to LP).
  if (chargerStatus1.ac_stat) {   // check if there is charging current.
    bACPresent = true;
    if (64 < chargeCurrent) {   // Check if there is charging current. Due to precision issues, current less than 64 is considered as fully charged.
      bCharging = true;
    } else {
      bCharging = false;
    }
    bDischarging = false;
  } else {
    bACPresent = false;
    bCharging = false;
    if (dischargeCurrent) {   // Check if there is discharging current.
      bDischarging = true;
    } else {
      bDischarging = false;
    }
  }

  iRunTimeToEmpty = (float)iAvgTimeToEmpty * iRemaining / 100;

  // Refresh the values to be reported on USB-HID based on the obtained charge chip data
  flashReportedData();
}

// Alert-only LED indication (LEDs are active low: LOW = on), by priority:
//   1. On battery and SOC critical: red/blue alternating fast
//   2. On battery: red slow blink
//   3. Host comms lost: blue blink
//   4. Charging with low battery: green solid
//   5. Normal (AC present, battery ok): all LEDs off
void updateLeds(void)
{
  unsigned long now = millis();

  // Critical latch with hysteresis; the exit check must run regardless of AC
  // state so a latch set on battery cannot survive a full charge cycle
  static bool bCritical = false;
  if (iRemaining < LED_CRITICAL_SOC) {
    bCritical = true;
  } else if (iRemaining >= LED_CRITICAL_SOC_CLEAR) {
    bCritical = false;
  }

  // Green charging band with hysteresis (SOC is ~1.5 points per ADC step)
  static bool bShowCharge = false;
  if (iRemaining <= LED_CHARGE_SHOW_SOC) {
    bShowCharge = true;
  } else if (iRemaining >= LED_CHARGE_HIDE_SOC) {
    bShowCharge = false;
  }

  bool green = false, red = false, blue = false;   // true = lit

  if (!bACPresent && bCritical) {
    bool phase = (now / LED_FAST_PERIOD_MS) & 1;
    red = phase;
    blue = !phase;
    ledPattern = F("red/blue alternating (on battery, critical)");
  } else if (!bACPresent) {
    red = (now / LED_SLOW_PERIOD_MS) & 1;
    ledPattern = F("red slow blink (on battery)");
  } else if (bCommsLost) {
    blue = (now / LED_FAST_PERIOD_MS) & 1;
    ledPattern = F("blue fast blink (comms lost)");
  } else if (bCharging && bShowCharge) {
    green = true;
    ledPattern = F("green solid (charging)");
  } else {
    ledPattern = F("off");
  }

  digitalWrite(UPS_GREEN_LED, green ? LOW : HIGH);
  digitalWrite(UPS_RED_LED, red ? LOW : HIGH);
  digitalWrite(UPS_BLUE_LED, blue ? LOW : HIGH);
}

// Blocking delay that keeps the LED blink patterns running
void delayWithLeds(unsigned long ms)
{
  unsigned long start = millis();
  while (millis() - start < ms) {
    updateLeds();
    sampleHostPowerLed();
    delay(25);
  }
}

