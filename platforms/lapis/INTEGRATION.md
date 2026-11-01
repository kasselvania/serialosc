# SerialOSC on Lapis: proposed integration contract

Status: implementation candidate with software tests, NOT an approved hardware-compatible release. USB startup fault unresolved. No active service, driver, or system installer is shipped. Historical requirements below are the design contract; README.md and SOFTWARE-EVIDENCE.md identify which software portions are now implemented and tested. Physical/Lapis lifecycle acceptance is still separate.

## 1. Separate the layers

- Kernel: use the existing Linux ftdi_sio and cdc_acm drivers. SerialOSC is a userspace protocol/OSC bridge, not a replacement USB kernel driver. Do not unload/rebind drivers during normal install/start.
- Lapis: preserve usb-gadget, lapis-mtp-responder, hwserviced, healthd, file-sharing consent, Type-C roles, charging and hub configuration. Gadget/file-transfer controller 23000000.usb is distinct from the observed Arc host path under 23400000.usb. Distinct controllers do not establish electrical independence.
- Generic host autoprobe: mtp-probe is distinct from the Lapis MTP responder. It ran twice on the last Arc attachment. Its sysfs endpoint prefilter may reject FTDI without deeper USB probing, so invocation does not prove a USB transfer. No global disable/removal is justified.
- Device application: SerialOSC opens selected ttys, sets termios, sends protocol messages and may change line behavior. It must not be described as passive or harmless merely because it is rootless.

## 2. Rollback boundary and shortcomings of the prototype method

The previous experiment cloned a pinned fork with submodules, configured with CMake, extracted Debian build dependencies to /tmp without installing them, and copied ARM64 binaries into this workspace. It did not run the Steam Deck installer or install a replacement FTDI driver. Three binaries were kept together because the supervisor locates helpers beside itself. A temporary managed host unit ran the daemon, and later short-lived test processes accessed the tty directly.

This proved compilation and loopback OSC only; it did not establish Arc hardware compatibility, startup electrical safety, a reproducible distributable, or Lapis lifecycle compatibility. Missing DNS-SD headers disabled Zeroconf. Debug probes exercised DTR/RTS and baud state without a verified Arc-specific reset model. That approach is retired, not promoted to an installer.

A failure to open raw USB controls is a permission boundary, not proof the hardware lacks support. No privilege bypass, blanket dialout grants, new root service or chmod on /dev is an acceptable integration workaround.

## 3. Build and packaging

Preferred first target: per-workspace Nix derivation, pinned to the device's nixpkgs revision 08d3a97586f6b6343b3b5e799980773629432610. This is a proposed build route: availability of a serialosc attribute and a successful full Nix build are NOT verified. Never install to the default profile; do not use sudo.

Pin SerialOSC commit and every submodule. The retired prototype used c96ea389dbf82c84d17f6f7adddaf311aed49438 (1.4.8); the Steam Deck hardware-tested baseline uses 94d457f80fe3721d21df5190c99bd522c711185a (1.4.7). Select one baseline explicitly; do not mix claims or overwrite the existing Steam Deck release. A build must emit source hashes, patches, architecture, dependency closure/linkage, compiler information, checksums and license/corresponding-source materials (including static liblo obligations).

An ARM64 Debian .deb is optional later, NOT the default Lapis installation path: this agent runtime has apt disabled in favor of per-workspace Nix. A plain Debian build does not automatically become a supported Lapis integration. Do not bundle SteamOS Distrobox installers or enforce x86-64 release hashes on ARM64.

Stage all three executables in a versioned private directory. Build/install performs zero hardware opens, does not launch the detector, does not enable services, and does not probe USB to validate installation. The default installation path must be inert. Generate a manifest and uninstall plan before installing.

## 4. Required guarantees before any active Lapis release

Selection, userspace bounds, explicit profiles and local binding now have candidate implementations. These requirements remain subject to the precise software/physical boundaries in SOFTWARE-EVIDENCE.md; they are not a claim of a hardware pass.

### Selection before opening

The reviewed upstream Linux detector accepts any tty with a usb-serial parent; libmonome later applies serial matching, but every tty can already become a candidate and a matching serial can be opened automatically. On Lapis the detector must filter BEFORE launching a worker, not rely on post-open classification.

Use a persisted, explicit allowlist of identity tuples including VID, PID, serial, and interface where needed. VID:PID 0403:6001 alone is a generic FTDI identity and is NOT sufficient. m1001113 can be a configured lab target, not a hardcoded universal Arc identifier. Re-resolve identity after reconnect and immediately before opening; ttyUSB numbers are not identities. Identity filtering is an operational boundary, not cryptographic authentication.

Provide an observe-only mode that never opens ttys. Starting active mode requires an explicit command selecting the approved target. Hardware validation and unattended/autostart remain separately opt-in, not consequences of installation. Keep serial access as narrow as supported by the platform; do not expose raw USB/FunctionFS/control nodes to a device application.

### Bounded initialization and correct serial behavior

- Add a monotonic overall initialization deadline, checked writes, retry limits and detailed pending-response diagnostics to mext_open. Do not relax Arc grid-size requirements without verified protocol evidence.
- Explicitly configure approved flow control instead of inheriting CRTSCTS/IXOFF/IXANY. Document HUPCL/DTR/RTS behavior, including unavoidable driver open/close effects; do not add reset toggles on speculation.
- No automatic B0, break, driver reset, USB reset, port power cycle, EEPROM/firmware writes, or power-limit changes in the normal runtime.
- Device readiness requires protocol identification and a ready OSC endpoint, not a live process or tty existence.
- Avoid reading/writing unrelated devices and wrong output families: ring messages only after verified Arc capability, grid messages only after verified grid capability.
- Failure must close safely with a bounded supervisor stop, log the cause and avoid indefinite init/restart loops. Kernel-level stalls may require external lifecycle supervision; a userspace deadline alone cannot guarantee interrupting a blocked kernel operation.

### Networking

Audit liblo socket binds. Retired supervisor uses lo_server_new on port 12002 and was observed listening on 0.0.0.0; loopback request success did not prove loopback-only exposure. A Lapis package should default to loopback-only discovery AND per-device OSC sockets, with implementation verified by negative remote-reachability tests. Remote OSC is a separate explicit option; expose only to a user-selected interface/peer policy. Do not change the Lapis firewall globally or imply unauthenticated OSC is safe on every interface.

Zeroconf should be optional. Decide explicitly between local OSC discovery without mDNS and supported Avahi/DNS-SD integration. If enabled, use existing policy and least exposure; never rewrite global Avahi settings to make a check pass.

## 5. Lapis lifecycle

Development: foreground explicit start or an identifiable Lapis managed child task with saved session/task ID. Managed child work creates a transient host service; describe this honestly. Stop through the lifecycle owner, verify terminal state and confirm no worker remains. No cron/reconnect shell loops.

Production: obtain a documented Lapis service/app lifecycle integration and permissions, with explicit install/start/stop/uninstall and recovery. A systemd --user service from the Deck is not automatically compatible: bus availability and host/sandbox boundaries must be tested. An agent may lack raw USB reset permissions even with dialout membership; do not install a privileged reset helper to hide this boundary.

Configuration belongs in a dedicated, versioned project/app state path, not shared defaults that silently replace another SerialOSC instance. One owner per target. Refuse to start if UDP 12002 is already owned, and never stop an unknown server to make room.

## 6. Startup-fault acceptance gate

Do NOT run more active probes until the abnormal startup is characterized. Installation must never be treated as a reason to reconnect hardware repeatedly.

1. Preserve exact cable/port identity, timestamps, user-observed boot behavior and read-only host logs.
2. Correlate kernel FTDI attach, Lapis hotplug, udev helpers and actual device accesses. Existing logs do not prove which process transferred bytes. Full access/USB tracing needs an administrator-approved method.
3. If warranted, perform a target-only exclusion of generic MTP probing or FTDI binding with an explicit manifest and rollback, while preserving Lapis gadget/file-sharing. Do not claim this was performed; current agent permissions do not allow it. Excluding mtp-probe is a diagnostic hypothesis, not a proven fix.
4. If failures persist, instrument VBUS voltage/ramp/inrush and reset behavior. Sysfs power_enabled/no-overcurrent cannot rule out a fast voltage dip or establish output-current rating. No authoritative USB-A amperage rating was established.
5. Only after normal startup is reproducible without application access, enable the allowlisted runtime and compare behavior. Require bounded init, verified protocol, OSC round trip, capability-correct LED output, encoder events, stop/start and reconnect tests. Test Arc alone before multi-device scenarios.
6. Restore application settings and touched LEDs only when the protocol is known working; do not send speculative cleanup bytes to an unresponsive device.

No hardware-pass claim until normal boot and physical input/output are witnessed on this Lapis. No assurance that a visually structured pattern must originate from a particular host program: treat that as a hypothesis requiring traffic evidence.

## 7. Uninstall and audit guarantees

Maintain file manifest, package-profile entry, lifecycle owner, config/state paths and all optional platform changes. Stop only owned tasks, remove only owned paths/profile entries, preserve user state unless explicitly requested, and never delete Lapis MTP/USB services or unrelated udev rules. Document irreversible history: serial bytes/control-line events are not undone by deleting binaries. No claim to factory-reset hardware state without measurement.

## Current project

- passive_doctor.py: sysfs read-only observations; no /dev opens, commands or service changes.
- tests/: synthetic-filesystem tests, no hardware.
- audit/: rollback manifests and inert archives, not installed/running code.
- No active SerialOSC binaries, patched daemon, installer or privileged helper is deployed.
