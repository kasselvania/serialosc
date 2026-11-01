# Explicit Lapis USB candidate policy

Opt-in software gate; **not authorization to start or probe hardware**.
No kernel or udev rule changes. Legacy/macOS detection is not changed.

## Launcher/build contract

A Lapis private build defines **SOSC_LAPIS_STRICT**. Its Linux detector rejects
absent, malformed or explicitly disabled strict policy before udev enumeration;
it never falls back to the historical broad usb-serial candidate selection.
Non-Lapis builds preserve legacy behavior only when strict is absent (or exactly
0) and *all* USB identity fields are absent. A partial identity never enables broad
detection. Setting strict=1 enables the same fail-closed gate in a generic build.

The launcher must populate these exact environment variables from a validated,
persisted per-device profile, without shell evaluation:

| Variable | Required strict value |
|---|---|
| LAPIS_SERIALOSC_STRICT | exactly 1 |
| LAPIS_SERIALOSC_USB_VID | exactly four hexadecimal characters |
| LAPIS_SERIALOSC_USB_PID | exactly four hexadecimal characters |
| LAPIS_SERIALOSC_USB_SERIAL | exact case-sensitive 1..128 character identity |
| LAPIS_SERIALOSC_USB_INTERFACE | exactly two hexadecimal characters, required even for interface 00 |

Serial syntax is deliberately narrow: ASCII letters, digits, period, underscore,
hyphen. No prefix matching, wildcards, whitespace, lists or friendly labels.
VID/PID/interface hex comparison is case-insensitive numerically. One tuple per
owned process is one allowlist entry; this is not multi-device list parsing.
Generic 0403:6001 alone is never sufficient. No lab serial is hardcoded. Empty
fields are present-but-invalid; unsupported/missing identity fails closed.
The profile persistence/schema and user-facing start permission belong to the
launcher, not the detector. Installing/building never grants start permission.

## API and ordering

Pure reusable policy: include/serialosc/lapis_usb_policy.h and
src/common/lapis_usb_policy.c. The caller initializes via
sosc_lapis_usb_policy_from_env(), requires mode SOSC_LAPIS_USB_STRICT, and matches
using sosc_lapis_usb_matches(). Pure parse accepts explicitly provided fields
without global environment for unit tests.

Linux detector uses USB device idVendor/idProduct/serial and USB interface
bInterfaceNumber from the candidate tty's actual ancestors before sending
connection IPC that could launch a worker. No tty is opened by selection.
Hotplug and initial enumeration share the same predicate. Unknown interfaces,
missing sysattrs and non-USB ttys are rejected in strict mode. Udev still
*enumerates metadata* for tty entries; this is not a claim that enumeration itself
is limited to one USB device.

The worker must link src/common/lapis_usb_linux.c and immediately before
monome_open call:

    sosc_lapis_usb_validate_devnode(&policy, devnode) == 1

A nonmatch must terminate before opening the tty. This helper resolves only
/sys/class/tty/<canonical tty basename>/device and reads bounded sysattrs from
at most 32 ancestors within canonical /sys. It refuses aliases such as
/dev/serial/by-id, missing/unresolved/escaped paths, first-device mismatch and
upstream-hub fallback. The tty pathname is never opened. validate_at() accepts a
synthetic sysfs root for tests. Production must use validate_devnode(), not an
attacker-controlled root override. Worker integration/guarding belongs to the
parent build/main change, not this helper.

This narrows but does not eliminate the sysfs-check-to-open race: unplug/replug,
renaming or another process can change a path between check and actual open.
This is operational identity filtering, not cryptographic authentication or an
atomic fd-bound identity guarantee. Post-open fd/sysfs association hardening
requires separate design; do not claim this makes a malicious USB device trusted.

## Software-only tests

    python3 platforms/lapis/tests/run-usb-policy-tests.py

Pure selector checks never read sysfs/open tty; synthetic sysfs tests read only
TemporaryDirectory fixtures. See patches/README.md for patched libmonome fake
transport coverage and remaining finite-I/O/kernel-close/physical-line caveats.
No active daemon/probe tests or real device access are needed or justified.
