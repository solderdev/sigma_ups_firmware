# LPUPS Firmware + Linux Shutdown Setup

Firmware for the LattePanda Sigma's onboard Arduino Leonardo (ATmega32U4),
handling the external UPS hat (4 Li-Ion cells). Reads the hat's charger chip
via I2C and presents the system as a standard **USB HID power device** (UPS
class, `3343:803a`), reporting charge percentage, runtime estimate and
AC/charging/discharging status.

- Build: `make build`; flash: `make flash` (opens the serial monitor
  afterwards). Requires `arduino-cli` plus two things the Makefile expects:
  the `lattepanda:avr` board core (copy the `avr` folder from LattePanda's
  "Leonardo Configuration Files" download, `Arduino IDE Files/avr-0.0.3/avr`,
  to `~/Arduino/hardware/lattepanda/avr`) and the
  [DFRobot_LPUPS](https://github.com/DFRobot/DFRobot_LPUPS) library cloned
  next to this repo (referenced as `../DFRobot_LPUPS`).
- Charge % is linear in pack voltage (12.4 V = 0 %, 16.7 V = 100 %), smoothed
  over a ~32 s rolling average so load-spike sag can't fake a low battery.
- Max charge voltage is limited to 15.6 V (3.9 V/cell) for battery longevity.
- The firmware's own low-battery flag only fires below ~8 % (runtime < 600 s),
  and it cannot cut output power — shutdown policy is therefore handled on the
  Linux side with NUT (see below).

## Host setup: shutdown at 20 % via NUT

NUT (Network UPS Tools) monitors the UPS over USB-HID, ignores the device's
own low-battery flag (`ignorelb`) and asserts low battery at **20 % charge**;
`upsmon` then powers the system off. Install with `sudo pacman -S nut` —
commands assume Arch Linux; on other distros adjust the package manager and
the driver path (`/usr/lib/nut/` may be `/lib/nut/` or `/usr/libexec/nut/`).

### 1. udev rule

```sh
sudo install -m 644 62-nut-lpups.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

Grants the `nut` user access to the device (DFRobot's USB vendor ID is not in
NUT's shipped rules) and restarts `nut-driver@lpups` whenever the board
(re-)enumerates — mandatory after reflashing, see "Restarting the services".

### 2. Config files in `/etc/nut/`

Five files, each shown below with its full contents. Replace `SECRET` in
`upsd.users` and `upsmon.conf` with the same generated password
(`openssl rand -hex 12`).

**`nut.conf`**
```
MODE=standalone
```

**`ups.conf`**
```
[lpups]
    driver = usbhid-ups
    port = auto
    vendorid = 3343
    productid = 803a
    # UPS is HID interface 2 of the composite device (0/1 = CDC serial),
    # interrupt endpoints 0x84 IN / 0x05 OUT (see lsusb -v -d 3343:803a).
    # Without the subdriver/usb_* lines NUT tries interface 0 (the serial
    # port), fails with "Unable to get HID descriptor (Pipe error)", and
    # interrupt reads time out on the wrong endpoint.
    subdriver = "Arduino HID.*"
    usb_hid_rep_index = 2
    usb_hid_ep_in = 4
    usb_hid_ep_out = 5
    desc = "LattePanda LPUPS (4 cells)"
    # device only signals LB below ~8%; enforce our own threshold instead
    ignorelb
    override.battery.charge.low = 20
```

**`upsd.conf`**
```
LISTEN 127.0.0.1 3493
```

**`upsd.users`**
```
[upsmon]
    password = SECRET
    upsmon primary
```

**`upsmon.conf`**
```
MONITOR lpups@localhost 1 upsmon SECRET primary
MINSUPPLIES 1
SHUTDOWNCMD "/usr/bin/systemctl poweroff"
POLLFREQ 5
POLLFREQALERT 5
DEADTIME 15
POWERDOWNFLAG /etc/killpower
# Require OB LB to persist ~3-4 polls before forcing shutdown, so a brief
# status glitch can't power the system off (the firmware also smooths the
# voltage; this is a host-side backstop).
OBLBDURATION 15
```

The files contain the password, so restrict them:

```sh
sudo chown root:nut /etc/nut/*.conf /etc/nut/upsd.users
sudo chmod 640 /etc/nut/*.conf /etc/nut/upsd.users
```

### 3. Test, enable, verify

```sh
sudo /usr/lib/nut/usbhid-ups -a lpups -d1 -DD 2>&1 | tail -40   # one debug poll cycle
sudo systemctl enable --now nut-driver@lpups.service nut-server.service nut-monitor.service
upsc lpups                                                      # charge, runtime, status
```

The debug run should end in a data dump (`battery.charge`, `ups.status: OL`,
...) including "using 'battery.charge' to set battery low state" — confirms
the 20 % threshold is active. `ups.status` reads `OL CHRG` (on line,
charging) or `OL` (full); on AC loss `OB`, below 20 % `OB LB`. Once `OB LB`
has persisted for `OBLBDURATION` (~15–20 s including poll timing), upsmon
shuts the system down.

### 4. Restarting the services

```sh
sudo systemctl restart nut-driver@lpups.service   # after ups.conf changes, reflash, or USB replug
sudo systemctl restart nut-server.service         # after upsd.conf / upsd.users changes
sudo systemctl restart nut-monitor.service        # after upsmon.conf changes
```

The driver restart after reflashing/replugging is mandatory: usbhid-ups 2.8.5
never reconnects once the USB device drops — it logs a single
`nut_libusb_get_interrupt: No such device` and idles, leaving `upsc` at
"Data stale" indefinitely (the process doesn't exit, so the unit's
`Restart=always` never kicks in either). The udev rule from step 1 automates
this: `make flash` then needs no manual follow-up.

### Useful commands

```sh
upsc lpups                                        # all UPS variables (charge, runtime, status)
upsc lpups ups.status                             # just the status (OL / OB / OB LB)
journalctl -u nut-driver@lpups -f                 # follow driver log (reconnects, stale data, USB errors)
journalctl -u nut-monitor -f                      # follow upsmon log (on-battery / low-battery / shutdown events)
make monitor                                      # firmware debug output on /dev/ttyACM0
lsusb -v -d 3343:803a                             # inspect the composite USB device (interfaces/endpoints)
sudo /usr/lib/nut/usbhid-ups -a lpups -d1 -DD     # one debug poll cycle without the service (stop it first)
systemctl status nut-driver@lpups nut-server nut-monitor   # health of all three services at a glance
sudo systemctl restart nut-driver@lpups           # kick the driver (fixes "Data stale"; see step 4 for the others)
```

### Testing the shutdown path

Either run a full drill with `sudo upsmon -c fsd` (**immediately** shuts
down), or temporarily set `override.battery.charge.low = 90` in `ups.conf`,
restart `nut-driver@lpups`, unplug AC and watch it power off ~15–20 s after
`OB LB` appears (`OBLBDURATION`). Revert to 20 afterwards. The firmware
smooths the voltage over ~32 s, so after unplugging AC the charge reading
takes up to half a minute to settle.

### Notes

- After the OS powers off, the UPS output stays on (batteries drain slowly to
  the charger chip's hardware cutoff). There is no remote output cut, so
  power does not cycle when AC returns — the optional chapter below works
  around this via the power button.
- NUT talks to the HID interface only; `/dev/ttyACM0` (serial debug/flashing)
  is unaffected.
- Reflashing the Arduino while the system is up is safe: the firmware waits
  for a valid charger-chip read before serving USB-HID data, so a
  (re)connecting driver never sees the bogus power-up values (which would
  read as "on battery, empty" and shut the host down).

## Optional: auto power-on after an outage

A low-battery shutdown is otherwise one-way: the UPS cannot cut or cycle host
power, so once AC returns the host stays off until someone presses the power
button. Two extra wires to the host's front-panel header let the firmware do
that press itself. **Fully optional** — with nothing connected, a "press"
pulses an unconnected pin and has no effect. If you do wire it, connect
*both* lines: the LED sense is one of the two interlocks that prevent
pressing the button on a running host.

### Wiring

- **D5 → PWR_SW** (the hot side, ~3.3 V idle; the header's other pin is GND —
  verify). A "press" pulls the line to GND open-drain for 300 ms; the line is
  high-Z at all other times.
- **D6 → PWR_LED+**, plus an external **~100 kΩ resistor from D6 to GND**, so
  a floating or broken sense wire reads "off" (the USB-comms interlock covers
  that false-off).

Prerequisites:

- The Arduino must stay powered in soft-off (S5). Enable both of these (may
  require the latest BIOS and EC firmware):
  - BIOS Setup → Advanced → Power Management → **MCU Power Control** → Enabled
  - BIOS Setup → Advanced → Power Management → **Always On 5V Pin Header** → Enabled

  Then verify before wiring: shut the host down normally — the UPS hat's LEDs
  must keep running.
- PWR_LED+ must be high-side switched (high in S0, low/floating in S5). If
  the board switches the cathode instead, D6 reads "on" permanently and the
  feature never fires.
- Set the BIOS option "State After G3" (restore on AC) to *Power On*: it
  covers full battery depletion, which powers down the Arduino too, so the
  firmware cannot help there.

### Behavior

- Arms only when the host is observed **down** (power LED off ≥ 60 s *and*
  USB comms lost) while AC is absent, or within 5 min of AC returning (a
  low-battery halt may finish just after the outage ends). A manual poweroff
  on mains therefore never triggers a press; one done on battery during an
  outage is indistinguishable from a NUT shutdown and will be rebooted.
- Presses once AC has been back and stable for 60 s; retries up to 3 times
  per outage, 60 s apart. A lit power LED or healthy USB comms vetoes the
  press at the last moment.
- State is visible in the periodic serial output (`pwrOn = ...`), along with
  the raw `host LED ADC` reading for calibrating the on-threshold
  (`PWR_LED_ON_ADC_MIN`, default 300 ≈ 1.5 V — with the host running, the
  reading should sit well above it).
