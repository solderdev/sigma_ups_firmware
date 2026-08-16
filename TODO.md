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
