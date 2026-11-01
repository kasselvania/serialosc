# Personal SerialOSC for Lapis — integration foundation

**Status: source/integration candidate, not a hardware-ready daemon release.**

Branch: feature/lapis-integration in kasselvania/serialosc.
Base: feature/leased-destinations at 7187832c349202b1a94a9b10080ae57d40069946.
This preserves opt-in lease protocol v1 rather than rebuilding the non-lease main branch.

The earlier Lapis prototype was built from main c96ea38 and did NOT contain these leases. It has been rolled back. Lapis has not passed Arc startup or hardware acceptance. This branch does not install, start, rebind, reset or probe a USB device. No compiled daemon release asset is supplied by this foundation.

## Hardware-free build/check

From repository root:

    cmake -S platforms/lapis -B build-lapis-offline -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-lapis-offline
    ctest --test-dir build-lapis-offline --output-on-failure

This separate CMake project builds only the pure lease timer/state test, not the top-level daemon or detector; Python tests use a synthetic filesystem. It requires CMake 3.27+, C17 compiler and Python 3. Source tree submodules are not needed for this offline target. Keep assertions enabled. A passing test is NOT daemon, USB, PlugData or power validation.

## Passive sysfs inspection (separate from tests)

    python3 platforms/lapis/passive_doctor.py

Bounded reads only. Does not open /dev, send serial/USB/OSC, bind ports, change power/roles, invoke commands or alter services. It cannot trace other host programs or certify output-current capacity/voltage quality.

## Documents

- INTEGRATION.md: proposed Lapis deployment, source changes still required and acceptance gates.
- LEASE-COMPATIBILITY.md: server/client branch pins, protocol and Mac-vs-Lapis evidence boundary.
- INSTALLER-REVIEW.md: why Steam Deck install/start/remove scripts must not be reused on Lapis.
- ROLLBACK.md: scoped cleanup record without publishing device identifiers, uploaded images, diagnostic archives or raw host logs.

No default branch, existing Mac/Deck branch or kernel driver is changed by this candidate. No system installer/autostart is introduced. Active Lapis development must first implement the selection-before-open, bounded initialization, line-policy and networking requirements; the startup fault remains unresolved.

## CI example (not enabled)

ci-offline.example.yml describes x86-64/ARM64 GitHub checks using only the offline target. It is intentionally not installed under .github/workflows: the connected token lacks the workflow scope. No GitHub CI run or release publication is claimed.
