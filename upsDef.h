
#ifndef __LPUPS_DEF_H__
#define __LPUPS_DEF_H__

#include <Arduino.h>

/**
 * Battery voltage range: 12.3V ~ 16.8V, in order to keep the battery stable at extreme values:
 * Assuming the battery voltage range is 12.4V ~ 16.7V, corresponding to battery capacity 0 ~ 100.
 * Note: You can adjust the battery capacity more accurately by correcting the voltage mutation with dischargeCurrent if interested.
 */
#define MIN_BATTERY_VOLTAGE   12400   // Lower battery voltage limit
#define MAX_BATTERY_VOLTAGE   16700   // Upper battery voltage limit

#define UPS_GREEN_LED   9    // Charging from low battery, green
#define UPS_RED_LED     10   // On-battery / critical alert, red
#define UPS_BLUE_LED    13   // Host comms lost / critical alert, blue

// LED alert thresholds (battery capacity %), with hysteresis against SOC jitter
#define LED_CRITICAL_SOC        40   // Enter critical alert below this when on battery
#define LED_CRITICAL_SOC_CLEAR  43   // Leave critical alert at or above this
#define LED_CHARGE_SHOW_SOC     50   // Show green while charging at or below this
#define LED_CHARGE_HIDE_SOC     53   // Hide green again at or above this
#define LED_SLOW_PERIOD_MS      1000 // Red on-battery blink half-period
#define LED_FAST_PERIOD_MS      250  // Critical alternation / comms-lost blink half-period

// Rolling-average window for pack voltage, in loop iterations (~2 s each).
// One VBAT ADC LSB (64 mV) is ~1.5 capacity points, so a single sample sagged
// by a load spike can cross the host's 20 % shutdown threshold; ~32 s of
// smoothing rides out spikes while adding negligible lag to a real discharge.
#define VBAT_SMOOTH_SAMPLES   16

#define MIN_UPDATE_INTERVAL   26 // Minimum update interval for USB-HID

#define DATA_LEN_MAX   0x24U
extern uint8_t regBuf[DATA_LEN_MAX];

extern DFRobot_LPUPS_I2C::sChargerStatus1_t chargerStatus1;

extern uint16_t dischargeCurrent, chargeCurrent;
extern uint16_t batteryVoltage, maxChargeVoltage;
extern byte iRemaining;
extern bool bCharging, bACPresent, bDischarging; // Whether charging, AC power present, discharging

extern uint16_t iVoltage;   // Battery voltage in centivolts, as reported via USB-HID

extern uint16_t iRunTimeToEmpty, iAvgTimeToEmpty;   // 12

extern uint16_t iPresentStatus;   // Now and previous device status.

void initPowerDevice(void);
void printChargeData(void);
void flashReportedData(void);

#endif /* __LPUPS_DEF_H__ */
