[//]: # ($FrauBSD: framework-autorotate/CHANGELOG.md 2026-10-07 14:01:26 -0700 Devin Teske $)

# Changelog

Newest first. Each section is a git tag; the bullets are what landed
in that tag (from the previous tag, or from the start of the
repository for 1.0).

## 1.1 (2026-10-07)

- Add CHANGELOG.md
- on an extended or rotated desk the stylus watcher stays up
  for the X session, because the stylus can appear after the
  three-minute window; a laptop-only desk still watches for
  180 seconds
- C and header sources live in `src/`; the manual lives in `man/`
- Format Makefile
- Pepper/fix SPDX where necessary

## 1.0 (2026-08-12)

- orient the Framework Laptop 12 panel from the EC accelerometer
  and TBMD, and remap ILIT touch and stylus to that panel
- recover greeter orientation and recenter the XDM login window;
  optional per-user chrome hook
- `-hold` and `-release` pause autorotation via
  `~/.framework_hold_autorotate`; at the greeter,
  `~root/.framework_hold_autorotate`
- Framework Laptop 13 Pro is out of scope; it is a clamshell
