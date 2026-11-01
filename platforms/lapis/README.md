# Personal SerialOSC for Lapis — unactivated software candidate

**Not a hardware-ready release. Arc startup on Lapis remains unresolved.**

Branch: feature/lapis-integration, based on the Mac lease branch 7187832.
No main/Mac/Deck branch, kernel driver, MTP, USB role, power limit or service is
changed. Build/test never installs or starts a daemon. Existing submodules remain
unchanged; strict builds patch disposable dependency copies in the build tree.

## Implemented software safeguards

- Checked atomic config replacement: persist free callback before new lease
  grant, fail closed on error, fsync file/rename/directory; same-token retry and
  renew do not rewrite storage. Process-crash evidence, not power-loss proof.
- Explicit strict per-serial grid/Arc2/Arc4 surface profile. No ring-count inference.
  Unknown blocks grants/output; release/expiry still discard callback authority.
- Detector exact VID/PID/serial/interface allowlist before worker launch; worker
  rechecks sysfs immediately before open. Not an atomic fd identity guarantee.
- Private bounded mext/read/write/error patch with fake transport/termios tests.
  No new reset/DTR/RTS commands; kernel stalls/physical line effects unverified.
- Strict supervisor/worker OSC sockets verified bound to 127.0.0.1 UDP. No DNS-SD.
- Standalone Python Arc workbench and explicit owned foreground launcher; no
  automatic activation, service installation or default user-preference writes.
- Always-on test checks work in Debug and Release/NDEBUG.

## Hardware-free checks

Pure timer + synthetic doctor (no submodules/network needed):

    cmake -S platforms/lapis -B /tmp/lapis-offline -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/lapis-offline
    ctest --test-dir /tmp/lapis-offline --output-on-failure

Full software protocol/surface/persistence/transport/workbench/launcher checks:

    cmake -S platforms/lapis/software-tests -B /tmp/lapis-software -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/lapis-software --parallel 2
    ctest --test-dir /tmp/lapis-software --output-on-failure

Repeat with Debug and with SOSC_LAPIS_STRICT=OFF for compatibility coverage.
The full software harness uses ephemeral network sockets and mocked monome
functions/pipes; real config tests use private temporary directories only.
It never links a USB detector or opens a real tty. See software-tests/README.md.

## Build the inactive strict candidate

Requires initialized source submodules, CMake 3.27+, C17 compiler, Python 3,
patch, pkg-config and libudev development files (Linux). Use user/workspace
build dependencies; never run the Deck APT/Distrobox installer on Lapis.

    cmake -S . -B /tmp/lapis-candidate -DCMAKE_BUILD_TYPE=Release -DSOSC_LAPIS_STRICT=ON
    cmake --build /tmp/lapis-candidate --parallel 2
    ctest --test-dir /tmp/lapis-candidate --output-on-failure

On Lapis, build_candidate.py resolves pinned Nix dependencies without installing
a profile or service and builds inside the checkout for launcher validation:

    python3 platforms/lapis/build_candidate.py

Compilation creates bin/lapis-candidate.json hashes and three executable files
but starts none of them. This is a candidate, not an installer/release asset.
The manifest is an ownership receipt, not signed authentication/certification.
Strict mode is Linux-only and disables zeroconf. Standard builds leave strict
selection/surface/network policy OFF; Mac native build/physical regressions are
not established by Linux compatibility tests. Windows durable lease grants are
explicitly unsupported/fail closed until a Windows atomic implementation exists.

## Tools and documents

- ARC-WORKBENCH.md: standalone exact-endpoint Arc protocol client; no PlugData.
- LAUNCHER.md: explicit identity/profile/owned foreground lifecycle (activation
  documentation is not permission to reconnect or run hardware now).
- USB-POLICY.md and patches/: selection/transport/network implementation limits.
- ACCEPTANCE-GATES.md and SOFTWARE-EVIDENCE.md: software receipts versus physical gates.
- LEASE-COMPATIBILITY.md: protocol/client pins; release versus TTL distinction.
- INSTALLER-REVIEW.md and ROLLBACK.md: preserve integrated Lapis infrastructure.
- passive_doctor.py: bounded sysfs-only observation; no /dev/OSC/service commands.

CI example remains inactive because the connected token lacks workflow scope.
No GitHub CI run, binary release/tag, Arc/PlugData/DAW/electrical acceptance is
claimed. Stop before physical testing until a separate attended test is approved.
