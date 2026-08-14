# TODO

## Smooth battery voltage to avoid premature shutdown on load spikes

Charge % is derived instantaneously from pack voltage
(`ups_firmware.ino:92`). While on battery, a heavy load spike can sag the
voltage below ~13.26 V (= 20 %) for a single sample. NUT shuts down on the
first `OB LB` observation with no debounce, and the shutdown latches — so one
transient dip powers the system off. The firmware guards against upward jumps
(`ups_firmware.ino:111-114`) but not downward dips.

Fix in firmware: apply a short rolling average (e.g. last 8–16 samples of
`batteryVoltage`, one sample per ~2 s loop) before computing `iRemaining`.
Feed the smoothed value into `iVoltage` too — that also stops ±1 ADC-LSB
jitter (64 mV) from triggering the send-on-change batch every ~2 s loop at
full charge, where the clamped charge % no longer absorbs it.

Not urgent: only act if premature shutdowns under load are actually observed.
