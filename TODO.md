# TODO

- Verify the PRESENTSTATUS retry fix after the next manual flash. Code change
  landed 2026-08-16: iPreviousStatus is now saved only when the PRESENTSTATUS
  send itself succeeded (iRes >= 0). (Independent review 2026-08-15,
  finding #1.)

  Test procedure (freeze driver polling so the two endpoint banks fill and
  only the PRESENTSTATUS send fails):

  1. Terminal A, firmware view: `cat /dev/ttyACM0` (CDC iface, doesn't
     disturb the NUT driver; baud irrelevant on native USB).
  2. Terminal B, host view, timestamped (fish):
     `while true; echo (date +%T) (upsc lpups ups.status 2>/dev/null); sleep 0.5; end`
  3. Baseline first: plain AC pull, nothing stalled — `OB DISCHRG` must
     appear in terminal B within ~2-4 s. Plug back in, wait for `OL`.
  4. Fault injection, terminal C:
     `sudo pkill -STOP -f usbhid-ups; sleep 5; sudo pkill -CONT -f usbhid-ups`
     and pull AC immediately after hitting enter.
  5. Pass: `OB DISCHRG` within ~3 s of the CONT (old behavior: up to ~8 s,
     next keepalive). Terminal A must show `iRes = -1` on the loop right
     after the pull, proving the partial failure was actually induced;
     if not, repeat.

  Caveats: keep the stall at ~5 s (beyond ~15 s upsd flags data stale and
  upsmon gets noisy). Margin is 3 s vs 8 s — repeat ambiguous runs.

# Host auto power-on: hardware bring-up

Firmware landed 2026-08-17 (D5 pulses PWR_SW open-drain, D6/A7 senses
PWR_LED+). Steps in order; step 1 is the go/no-go gate.

1. Go/no-go, before any wiring: shut the host down normally. If the blue
   LED starts fast-blinking ~20 s later, the 32u4 stays alive in S5 and the
   feature can work. If everything goes dark, stop — approach is dead.
2. Multimeter on PWR_LED+: must read high in S0 and low/floating in S5
   (confirms high-side switching; if the board switches the cathode instead,
   D6 would read "on" forever and the feature silently never fires).
3. Wire D5 → PWR_SW hot side (~3.3 V idle; other pin is GND — verify).
   Pressing = firmware pulls it to GND, open-drain, 300 ms.
4. Wire D6 → PWR_LED+, plus external ~100 kΩ from D6 to GND (floating or
   broken wire then reads "off"; comms interlock covers the broken-wire
   false-off).
5. Flash, watch `host LED ADC` on serial with the host running: on-value
   should sit well above the threshold (PWR_LED_ON_ADC_MIN = 300 ≈ 1.5 V).
   A parallel red LED can clamp the node near ~1.8 V — if margin is thin,
   lower the threshold to ~200.
6. BIOS: set "State After G3" / restore-on-AC = Power On. Covers full
   battery depletion, which kills the 32u4 too; firmware can't help there.
7. Dry run — order matters (replugging AC before the host is down clears
   the latch by design): unplug AC → `systemctl poweroff` on battery →
   wait ~2 min for arming → replug AC → D5 pulses ~1-2 min later, host
   boots.
8. Negative test: `systemctl poweroff` on mains, no outage → host stays off.
9. Full test: pull AC, let NUT reach shutdown (or `upsmon -c fsd` on
   battery), replug → host comes back. Afterwards check `upsc lpups`:
   iDelayBe4ShutDown stays > 0 after an FSD (pre-existing quirk, keeps
   SHUTDOWNREQ/SHUTDOWNIMNT set) — confirm NUT ignores it.
