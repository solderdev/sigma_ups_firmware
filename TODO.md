# TODO

## Fix battery.voltage reporting (cosmetic)

`upsc lpups` shows `battery.voltage: 167.00` and `battery.voltage.nominal:
167.00`. Both values are wrong/static:

- **Factor 10 off:** the HIDPowerDevice report descriptor declares the voltage
  field in centivolts, but `fun.cpp` writes millivolts (`MAX_BATTERY_VOLTAGE` =
  16700). NUT scales this to 167.00 V instead of 16.7 V. Correct raw value:
  1670 (centivolts).
- **Never updated:** `iVoltage` is set once at boot to `MAX_BATTERY_VOLTAGE`
  and never refreshed with the measured `batteryVoltage` from the charger chip.
  The loop only re-sends RemainingCapacity, RunTimeToEmpty and PresentStatus.

Fix in firmware:
- Set `iConfigVoltage = 1670` (nominal, centivolts).
- Each loop: `iVoltage = batteryVoltage / 10;` and send it via
  `PowerDevice.sendReport(HID_PD_VOLTAGE, &iVoltage, sizeof(iVoltage));`

No functional impact: NUT's shutdown logic uses `battery.charge` (20 %
threshold via `ignorelb`), not voltage. Purely cosmetic.

## Smooth battery voltage to avoid premature shutdown on load spikes

Charge % is derived instantaneously from pack voltage
(`ups_firmware.ino:77`). While on battery, a heavy load spike can sag the
voltage below ~13.26 V (= 20 %) for a single sample. NUT shuts down on the
first `OB LB` observation with no debounce, and the shutdown latches — so one
transient dip powers the system off. The firmware guards against upward jumps
(`ups_firmware.ino:96-99`) but not downward dips.

Fix in firmware: apply a short rolling average (e.g. last 8–16 samples of
`batteryVoltage`, one sample per ~2 s loop) before computing `iRemaining`.

Not urgent: only act if premature shutdowns under load are actually observed.
