# Software candidate receipt — 2026-11-01

**Software-only candidate, not hardware acceptance or a release.**
Parent integration tests run on Debian13/aarch64, GCC14.2.0, CMake3.31.6,
Python3.13. All test checks remain enabled with -O3 -DNDEBUG.

## Implemented and exercised

| Area | Software evidence |
| --- | --- |
| Release checks | Always-on TEST_CHECK, single-evaluation self-test and deliberate bad check must exit nonzero with exact diagnostic |
| Durable grant | Real temporary config files, atomic checked save, stage fault injection, SIGKILL before/after rename, reload from same directory, no default preferences; failed persistence never grants |
| Strict surface | Arc2 indices0–1, Arc4 indices0–3, grid, unknown/invalid; continue attempted known cleanup after errors; release/expiry clear authority even on error |
| Ordinary OSC output | Production mext registration/dispatch against fake monome, strict family/ring/pixel/value/arity/type bounds and changed-prefix registration; default-policy regression |
| Lease deadlines | Fake clock crosses TTL inside device read: production event callbacks expire before sending; legacy host/port setters normalize before capturing destination; strict shutdown clears callback even on dark failure |
| USB selection | Pure exact VID/PID/serial/interface predicates, synthetic bounded sysfs ancestry/path/missing/mismatch/NUL tests; production preopen character-node/rdev checks |
| Transport | Private patched production mext/POSIX/Linux code with fake open/read/write/termios/poll/clock: silence, partial/error/EINTR/EAGAIN/EOF/flood, short query/output write, finite budgets; no real tty |
| Local networking | Actual kernel UDP bind127.0.0.1 + local OSC + nonloopback address negative delivery test with positive control; stock-liblo guard rejected |
| Standalone workbench | Fake clock/UDP framing, identity, acquire/renew/release/expiry/loss/timeouts; actual production sys/lease/runtime fixture interoperability with in-memory Arc2 output and real temp config |
| Owned launcher | Inert validation, generated manifest roundtrip, hashes/schema/permissions, exact environment, locks, dummy natural/nonzero/signal/stubborn-child cleanup; unrelated child survives |
| Supervisor shutdown | Production supervisor fixture, ephemeral local socket only, inert native-ABI child helpers: should-exit IPC, pending-state races, no respawn/late attach, child reaping, clean libuv close; stuck worker intentionally needs launcher fallback |
| Patching | Expected baseline content hashes + fuzz0; wrong baseline rejected before copying; tracked dependency submodules unchanged |

## Reproduction lanes and observed outcomes

From repository root:

    cmake -S platforms/lapis/software-tests -B /tmp/lapis-strict-software-release -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/lapis-strict-software-release --parallel 4
    ctest --test-dir /tmp/lapis-strict-software-release --output-on-failure

Repeat with Debug and a separate -DSOSC_LAPIS_STRICT=OFF Release tree.
Strict Debug and Release: **18/18 CTest cases passed**. Default-policy Release:
**14/14 cases passed** (strict transport/supervisor fixtures intentionally absent).
Each Python suite contains multiple individual unittest cases; see full CTest
LastTest.log or the attached receipts, rather than equating one CTest case with
one protocol scenario. The isolated supervisor suite has its own detailed log.

Pure no-network target:

    cmake -S platforms/lapis -B /tmp/lapis-offline -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/lapis-offline
    ctest --test-dir /tmp/lapis-offline --output-on-failure

Pure core plus synthetic doctor CTest cases passed. The independent transport
runner also passed:

    python3 platforms/lapis/tests/run-usb-policy-tests.py

Full top-level default/strict ARM64 daemon **compiled** and existing six root
CTest cases passed. Nothing in build or CTest executes the real detector/device
entrypoints. Prior missing pkgconfig/libudev-dev blocker was resolved with Nix
build --no-link, not APT or installation/service changes. Pinned dependency
resolution and inactive local build:

    python3 platforms/lapis/build_candidate.py

This helper uses nixpkgs08d3a97586f6b6343b3b5e799980773629432610 for systemd.dev and
pkg-config. Existing native toolchain is recorded, not a fully hermetic Nix
package closure. No reproducible-bit-for-bit, portable binary distribution or
production Nix derivation claim is made. Local ignored build artifacts stay
in build-lapis-candidate; Git contains source/tests only.

## Explicit limitations

- Physical Arc disconnected: no serial port opened, production detector/worker
  launched against hardware, reset/driver/power/MTP/udev/service change performed.
- Normal startup remains unresolved. No normal animation, real LED pattern,
  encoder event, reconnect, physical TTL darkness or electrical measurement pass.
- Explicit profile is operator-supplied metadata, not an automatic capability API.
- Sysfs precheck is not atomic fd identity or hostile-device authentication.
- Finite userspace I/O cannot cancel stalled kernel open/read/termios/write/close.
- SIGKILL/restart is process-crash evidence, not electrical power-loss durability.
- Free-after-no-renew readback without token-qualified expiry notification is
  not uniquely attributed expiry; workbench reports that distinction.
- Loopback is not authentication; legacy setters/ordinary output remain a local
  compatibility interface, not token-authorized LED access.
- Mac paths/default strictOFF behavior are retained; native Mac/Windows builds
  and physical regression are not performed. Windows durable lease grants fail
  closed pending a native implementation; no Windows compatibility pass claimed.
- Launcher group ownership is cooperative, not cgroup containment; crashes,
  SIGKILL, escaped descendants and kernel stalls remain documented limitations.
- PlugData/DAW/plugin/bypass/teardown scheduling and host Lapis actual lifecycle
  acceptance remain for later tests. No installer, autostart, release/tag/assets.

The first integration audit identified genuine output-dispatch/deadline gaps;
they were fixed and received targeted tests, not hidden by passing old suites.
Keep this software receipt separate from ACCEPTANCE-GATES.md physical results.

## Allocation-boundary deadline follow-up

Final review found that initial normalization alone did not cover TTL crossing
during address/string allocation. Legacy displacement now rechecks after its
preparation, discards captured leased components on expiry, and setters rebuild
from the post-displacement host/port when needed. Linker-wrapped allocation
regressions cross exactly at address creation and each displacement string copy
for both setters. Strict Debug/Release18 and default-policy Release14 suites
passed again; inactive full candidate build/root6 passed. This corrects a real
software race; it changes no physical acceptance claim.
