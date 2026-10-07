[//]: # ($FrauBSD: framework-autorotate/README.md 2026-10-07 14:01:26 -0700 Devin Teske $)

# framework-autorotate

FreeBSD X11 display autorotate for **Framework Laptop 12**.

The daemon reads orientation from the Chrome EC (memmap accelerometer +
tablet-mode bit), applies `xrandr` to the built-in panel, remaps absolute
touch/stylus to that panel, and optionally runs a per-user chrome hook.
It is safe with an XDM greeter and with extended external heads (only
eDP/LVDS/DSI is rotated).

**Out of scope:** Framework Laptop 13 Pro (even with the touchscreen display
kit) is a clamshell, not a convertible (no 360 hinge / tablet mode).
Touchscreen alone is not a reason to port this daemon; FW13 Pro also uses a
different touch HID (`CSW1322` vs FW12's `ILIT2901`).

Home: [FrauBSD/framework-autorotate](https://github.com/FrauBSD/framework-autorotate)

## Requirements

- Framework Laptop 12 under FreeBSD
- X11 (`xrandr`, `xinput`, `xdpyinfo`)
- `/dev/io` for EC LPC / memmap access (no kernel module required at runtime)

## Build / install

```sh
make
make install          # PREFIX=/usr/local by default
```

Installs:

| Path | Role |
|------|------|
| `sbin/framework_autorotate` | daemon |
| `libexec/framework_autorotate/xlogin_recenter` | place XDM greeter on the built-in panel |
| `libexec/framework_autorotate/map-touchscreen` | ILIT touch/stylus CTM after RandR |
| `etc/rc.d/framework_autorotate` | boot service |
| `share/man/man8/framework_autorotate.8` | manual |

## Enable

```sh
sysrc framework_autorotate_enable=YES
sysrc framework_autorotate_mode=always   # or: tablet
service framework_autorotate start
```

- `mode=always` = rotate whenever the accelerometer says so  
- `mode=tablet` = rotate only while EC TBMD=1; otherwise force `normal`

## Lock orientation (`-hold` / `-release`)

The daemon runs as root. Orientation can be paused without stopping the
service by creating a hold file for the account that owns the console:

```sh
framework_autorotate -hold        # touch ~/.framework_hold_autorotate
framework_autorotate -v -hold     # same, with a confirmation line
framework_autorotate -release     # remove that file
```

- **Logged-in session:** run as the session user -> `~/.framework_hold_autorotate`
  (no sudo; uses the real uid's passwd home, not a spoofed `$HOME`).
- **Greeter (XDM login):** run as root -> `~root/.framework_hold_autorotate`.

While that file exists, the daemon skips orientation changes. With logging
enabled (`framework_autorotate_logfile` set), verbose output reports
`orientation locked by /path/to/.framework_hold_autorotate`.

The hold basename intentionally does not tab-complete as a longer form of
the chrome hook (`~/.framework_autorotate`).

## Per-user chrome hook (optional)

Most desktops (KDE, GNOME, XFCE) follow RandR without further help.
For WMs that need a chrome rebuild after rotate, create
`~/.framework_autorotate` (spirit of `.xinitrc`).  See
`examples/dot.framework_autorotate.fvwm`.

## See also

`framework_autorotate(8)`
