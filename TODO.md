# TODO

- Retry partially failed report bursts. When PRESENTSTATUS fails but another
  report in the burst succeeded, the changed status word is consumed
  (iPreviousStatus is saved) and only reaches the host on the next keepalive
  burst — up to ~8 s later, and only probabilistically bounded. Fix: save
  iPreviousStatus only when the PRESENTSTATUS send itself succeeded
  (iRes >= 0) so the change retries next loop. Behavior change: needs a
  flash and a fault-injection re-verification. (Independent review
  2026-08-15, finding #1.)
