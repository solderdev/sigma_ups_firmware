# LPUPS Firmware + Linux Shutdown Setup

Firmware for the Arduino Leonardo (ATmega32U4) on the LattePanda Sigma UPS hat
(4 Li-Ion cells). Reads the charger chip via I2C and presents the system as a
standard **USB HID power device** (UPS class, `3343:803a`), reporting charge
percentage, runtime estimate and AC/charging/discharging status.

- Build: `make build` — flashing is done manually, then `make monitor`.
- Charge % is derived linearly from pack voltage (12.4 V = 0 %, 16.7 V = 100 %).
- Max charge voltage is limited to 15.6 V (3.9 V/cell) for battery longevity.
- The firmware's own low-battery flag only fires below ~8 % (runtime < 600 s),
  and it cannot cut output power — shutdown policy is therefore handled on the
  Linux side with NUT (see below).

## Host setup: shutdown at 20 % via NUT

NUT (Network UPS Tools) monitors the UPS over USB-HID. The driver is told to
ignore the device's own low-battery flag (`ignorelb`) and instead assert
low-battery at **20 % charge**; `upsmon` then powers the system off.

### 1. Install

```sh
sudo pacman -S nut
```

### 2. udev rule

DFRobot's USB vendor ID is not in NUT's shipped rules, so the `nut` user needs
access granted manually. Create `/etc/udev/rules.d/62-nut-lpups.rules`:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="3343", ATTR{idProduct}=="803a", MODE="0660", GROUP="nut"
```

Then reload: `sudo udevadm control --reload && sudo udevadm trigger`

### 3. Config files in `/etc/nut/`

Generate a password for NUT's internal user: `openssl rand -hex 12`
(replace `SECRET` below in both files that use it).

`nut.conf`:
```
MODE=standalone
```

`ups.conf`:
```
[lpups]
    driver = usbhid-ups
    port = auto
    vendorid = 3343
    productid = 803a
    subdriver = "Arduino HID.*"
    # UPS is HID interface 2 of the composite device (0/1 = CDC serial),
    # interrupt endpoints 0x84 IN / 0x05 OUT (see lsusb -v -d 3343:803a)
    usb_hid_rep_index = 2
    usb_hid_ep_in = 4
    usb_hid_ep_out = 5
    desc = "LattePanda LPUPS (4 cells)"
    # device only signals LB below ~8%; enforce our own threshold instead
    ignorelb
    override.battery.charge.low = 20
```

The `subdriver`/`usb_*` lines are required: without them NUT tries interface 0
(the serial port) and fails with "Unable to get HID descriptor (Pipe error)",
and interrupt reads time out on the wrong endpoint.

`upsd.conf`:
```
LISTEN 127.0.0.1 3493
```

`upsd.users`:
```
[upsmon]
    password = SECRET
    upsmon primary
```

`upsmon.conf`:
```
MONITOR lpups@localhost 1 upsmon SECRET primary
MINSUPPLIES 1
SHUTDOWNCMD "/usr/bin/systemctl poweroff"
POLLFREQ 5
POLLFREQALERT 5
DEADTIME 15
POWERDOWNFLAG /etc/killpower
```

Config files contain the password, so restrict them:

```sh
sudo chown root:nut /etc/nut/*.conf /etc/nut/upsd.users
sudo chmod 640 /etc/nut/*.conf /etc/nut/upsd.users
```

### 4. Test the driver once (before enabling services)

```sh
sudo /usr/lib/nut/usbhid-ups -a lpups -d1 -DD 2>&1 | tail -40
```

This runs a single poll cycle with debug output. On success it ends with a
data dump (`battery.charge`, `ups.status: OL`, ...) including the line
"using 'battery.charge' to set battery low state", which confirms the 20 %
threshold logic is active.

### 5. Enable services

```sh
sudo systemctl enable --now nut-driver@lpups.service nut-server.service nut-monitor.service
```

### 6. Verify

```sh
upsc lpups                       # battery.charge, battery.runtime, ups.status
```

`ups.status` should be `OL CHRG` (on line, charging) or `OL` (full). On AC
loss it becomes `OB` (on battery), and at <20 % it becomes `OB LB`, upon which
upsmon shuts the system down within one poll cycle (~5 s).

### Testing the shutdown path

Either run a full drill with `sudo upsmon -c fsd` (**immediately** shuts down),
or temporarily set `override.battery.charge.low = 90` in `ups.conf`, restart
`nut-driver@lpups`, unplug AC and watch it power off. Remember to revert to 20.

### Notes

- After the OS powers off, the UPS output stays on; the batteries drain slowly
  until the charger chip's hardware cutoff. The firmware does not implement
  remote output cut, so power does not cycle when AC returns.
- NUT talks to the HID interface only; `/dev/ttyACM0` (serial debug/flashing)
  is unaffected.
