# Lapis-only bounded libmonome candidate

**Software candidate only; no USB, electrical, kernel-stall, Mac or physical Arc acceptance.**

The git submodule is untouched. Baseline libmonome pin:

**9055ea22fd824839be821c10f6e9b13e9d2aa3cd**.

Apply only to a new private build-source directory, from the repository root:

    python3 platforms/lapis/patches/apply-libmonome.py third-party/libmonome /tmp/new-lapis-libmonome

The helper checks exact SHA-256 content of all three touched source files, refuses
an existing destination, excludes .git, and dry-runs with --fuzz=0 before applying.
The private copy is disposable; a failed apply does not edit the source submodule.
No compiler, daemon, tty or USB access is invoked by the application helper.
Use the private source in the Lapis build and define
**LAPIS_SERIALOSC_BOUNDED_MEXT** for libmonome and embedded mext (or its protocol
module). Merely carrying/applying the patch without this compile define does not
enable it. The parent build owns CMake integration; this helper does not install.

## Implemented policy

- Existing three mext initialization response requirements remain: query, ID,
  grid size. Arc-specific protocol interpretation/count acquisition is **not**
  invented or relaxed. Query response completeness is still the upstream meaning
  (first query response satisfies the query bit), not a new capability API.
- 3000ms CLOCK_MONOTONIC budget, at most 12 query rounds, at most 64 frames per
  round; clocks checked before and after read. Check every query write, including
  short writes. Failures close the initialized transport once, preserve errno,
  and print pending mask/round/error without printing serial identity.
- Normal mext output also rejects short writes; never retries/replays partial
  frames. Each transport write is a single O_NONBLOCK-fd syscall, not a drain or
  unbounded retry loop. Normal event input limits continuous system-frame work
  to avoid an endlessly busy peer trapping the event loop inside next_event.
- POSIX read uses a 25ms overall per-call budget plus 256-attempt ceiling;
  handles EOF, partial progress, EINTR, EAGAIN and hard errors finitely.
  A frame with truncated payload fails, rather than inventing bytes.
- Linux poll handles syscall failure, POLLERR, POLLHUP and POLLNVAL as failures.
- Check tcgetattr, tcsetattr and tcflush; retain 115200 (or existing quirk baud),
  raw 8N1 and explicitly clear IXON/IXOFF/IXANY and CRTSCTS when available.
  Preserve HUPCL. No new DTR/RTS ioctl, B0, break, reset, power, driver or udev
  operation. Software termios checks do **not** establish an electrically safe
  physical-device line policy or prove absence of driver open/close line effects.

These are userspace progress limits, **not** a promise to interrupt a stalled
kernel open/read/write/termios/close call or guarantee a 3000ms wall-clock return.
A read may extend initialization slightly beyond the deadline before it is
checked again. Close on failure may itself block. An external owned lifecycle
supervisor and physical acceptance remain required; do not compensate with a
restart loop or privileged resets. Existing unrelated libmonome init allocation
cleanup issues and non-mext protocols are not fixed by this patch.

## Hardware-free reproduction

    python3 platforms/lapis/tests/run-usb-policy-tests.py

Needs Python 3, cc and patch; uses temporary private copies. Tests use faked
monome transport, faked POSIX read/write/open/close/termios/clock and fake poll;
never launch a daemon, call production monome_open or open a tty. The runner
compiles but never executes unmocked transport. It also runs pure selector tests
with and without NDEBUG and synthetic sysfs revalidation checks, verifies source
hashes unchanged, refuses wrong baselines/existing destinations, and syntax
compiles legacy and enabled patched sources. Passing this is not daemon ABI,
hardware readiness, supervisor stop latency or electrical evidence.

Configured sysfs-test integration (no active sysfs/tty reads):

    cc -std=c17 -fPIC -shared -Iinclude src/common/lapis_usb_policy.c src/common/lapis_usb_linux.c -o /tmp/lapis-usb-policy.so
    LAPIS_USB_POLICY_LIBRARY=/tmp/lapis-usb-policy.so python3 platforms/lapis/tests/test_usb_sysfs.py

Ordinary Python discovery skips these tests without LAPIS_USB_POLICY_LIBRARY.
An explicitly configured library load failure remains a test failure, not a skip.
