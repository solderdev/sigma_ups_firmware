
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

// Host auto power-on: D5 pulses the Sigma's front-panel PWR_SW header
// (open-drain: INPUT idle, OUTPUT-low = press; never driven high), D6/A7
// senses PWR_LED+ through an external ~100k pull-down so a floating or
// disconnected line reads "off".
#define HOST_PWR_BTN_PIN   5
#define HOST_PWR_LED_ADC   A7     // A7 is the ADC channel of digital pin 6
#define PWR_LED_ON_ADC_MIN 300    // ~1.5 V at 5 V AREF; calibrate via serial ADC output
#define PWR_LED_ON_SAMPLES 3      // Consecutive on-reads to count as "on": one coupled
                                  // noise spike on the 100k node must not reset the
                                  // off-timer; 75 ms still catches any S3 blink phase

#define PWR_BTN_PULSE_MS   300      // Momentary press: above debounce, far below 4 s force-off
#define PWRON_LED_OFF_MS   60000UL  // LED continuously off this long = host is down
#define PWRON_ARM_GRACE_MS 300000UL // Host may still be finishing its halt this long after AC returns
#define PWRON_AC_STABLE_MS 60000UL  // AC back this long before pressing ("ondelay")
#define PWRON_RETRY_MS     60000UL  // Between press attempts
#define PWRON_MAX_TRIES    3        // Per outage cycle; reset on arm and on clear

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

// Comms-lost threshold: no report delivered into a free endpoint bank for
// this long. A single USB_Send waits only 250 ms for a bank while NUT drains
// the endpoint on a ~2 s poll cycle, so individual send failures are normal;
// only a sustained failure to deliver anything means the host is gone.
// The endpoint is double-banked: up to two sends can still succeed after the
// host stops reading, so worst-case detection is one keepalive period plus
// this timeout (~23 s), best case ~14 s.
#define COMMS_LOST_TIMEOUT_MS 15000UL

// Keepalive: force a report burst when nothing has been delivered for this
// long, so a healthy host resets the comms-lost clock well before the
// timeout. Must be enough below COMMS_LOST_TIMEOUT_MS to fit a few ~2 s
// retries. This is also the periodic send cadence: reports go out at least
// every ~8-10 s even when no value changes.
#define COMMS_KEEPALIVE_MS 8000UL

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
