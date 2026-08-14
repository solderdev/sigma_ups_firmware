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

int iIntTimer = 0; // Update interval counter

bool bCommsLost = false;   // Last HID report to the host failed


void setup(void)
{
  // Initialize UPS indicator LEDs first, so they stay dark through the boot delay.
  // Writing HIGH before pinMode keeps the active-low LEDs off with no on-glitch.
  digitalWrite(UPS_GREEN_LED, HIGH);
  digitalWrite(UPS_RED_LED, HIGH);
  digitalWrite(UPS_BLUE_LED, HIGH);
  pinMode(UPS_GREEN_LED, OUTPUT);
  pinMode(UPS_RED_LED, OUTPUT);
  pinMode(UPS_BLUE_LED, OUTPUT);

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

   // Initialize HIDPowerDevice
  initPowerDevice();
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

  // Report measured pack voltage in centivolts (HID descriptor unit).
  // Keep the last good value if the VBAT ADC reads 0 (dead ADC, or an I2C
  // failure on the very first loop while regBuf is still zeroed; later I2C
  // failures leave regBuf stale rather than zero).
  if (batteryVoltage) {
    iVoltage = batteryVoltage / 10;
  }

  /*********** Unit of measurement, measurement unit ****************************/
  /**
   * Battery voltage range: 12.3V ~ 16.8V, in order to keep the battery stable at extreme values:
   * Assuming the battery voltage range is 12.4V ~ 16.7V, corresponding to battery capacity 0 ~ 100.
   * Note: You can adjust the battery capacity more accurately by correcting the voltage mutation with dischargeCurrent if interested.
   */
  if (batteryVoltage > MIN_BATTERY_VOLTAGE) {
    iRemaining = (((float)batteryVoltage - MIN_BATTERY_VOLTAGE) / (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE)) * 100;
  } else {
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
    if (iPrevRemaining < iRemaining) {
      if (3 >= (iRemaining - iPrevRemaining))
        iRemaining = iPrevRemaining;
    }

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

  /************ Delay ***************************************/
  delayWithLeds(1000);
  iIntTimer++;
  delayWithLeds(1000);
  iIntTimer++;

  /************ Batch send or send on change ***********************/
  if ((iPresentStatus != iPreviousStatus) || (iRemaining != iPrevRemaining) ||
    (iRunTimeToEmpty != iPrevRunTimeToEmpty) || (iVoltage != iPrevVoltage) ||
    (iIntTimer > MIN_UPDATE_INTERVAL)) {

    // 12 INPUT OR FEATURE(required by Windows)
    PowerDevice.sendReport(HID_PD_REMAININGCAPACITY, &iRemaining, sizeof(iRemaining));
    if (bDischarging) PowerDevice.sendReport(HID_PD_RUNTIMETOEMPTY, &iRunTimeToEmpty, sizeof(iRunTimeToEmpty));
    PowerDevice.sendReport(HID_PD_VOLTAGE, &iVoltage, sizeof(iVoltage));
    iRes = PowerDevice.sendReport(HID_PD_PRESENTSTATUS, &iPresentStatus, sizeof(iPresentStatus));

    bCommsLost = (iRes < 0);   // Reporting return value: less than 0 indicates communication loss with the host

    iIntTimer = 0; // Reset reporting interval timer
    iPreviousStatus = iPresentStatus; // Save new device status
    iPrevRemaining = iRemaining; // Save new battery remaining capacity
    iPrevRunTimeToEmpty = iRunTimeToEmpty; // Save new estimated battery runtime count
    iPrevVoltage = iVoltage; // Save new reported battery voltage

  }

  /************ Serial print reported battery level and operation result ******************/
  Serial.print("iRemaining = "); // Battery remaining capacity percentage
  Serial.println(iRemaining);
  Serial.print("iRunTimeToEmpty = "); // Estimated time to empty battery
  Serial.println(iRunTimeToEmpty);
  Serial.print("iRes = "); // Reporting return value, less than 0: indicates communication loss with host
  Serial.println(iRes);
  Serial.println();
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
  } else if (!bACPresent) {
    red = (now / LED_SLOW_PERIOD_MS) & 1;
  } else if (bCommsLost) {
    blue = (now / LED_FAST_PERIOD_MS) & 1;
  } else if (bCharging && bShowCharge) {
    green = true;
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
    delay(25);
  }
}

