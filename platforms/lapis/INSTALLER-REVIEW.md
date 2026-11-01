# Existing installer review: Lapis compatibility

Reviewed upstream repository: https://github.com/kasselvania/serialOSC-steam-deck
The installer was fetched for inspection only. It was NOT executed on Lapis.

## Actual install.sh behavior

Source: https://github.com/kasselvania/serialOSC-steam-deck/blob/main/install.sh

- Lines 100-104 require Linux, x86_64, systemctl, ldd and ldconfig.
- Lines 110-112 require host libdns_sd.so.1.
- Lines 114-124 refuse certain legacy service configurations.
- Lines 128-132 verify executable linkage and exact 1.4.7 (94d457f) version.
- Lines 136-152 create user directories, stop the same-named user service, back up a differing unit, and copy the daemon/helpers/service/diagnostic/test/uninstall files into shared user locations.
- Lines 154-155 reload the user manager and ENABLE/START the service immediately.
- The status check establishes service activity, not normal device startup or hardware readiness.

The user-service approach is intentional on SteamOS, but for Lapis installation and hardware activation must be separate. A same-named shared service should not be stopped/replaced implicitly. Lapis agent sandbox access to a user bus is not guaranteed. Exact x86-64 binary receipts cannot validate an ARM64 build. A host DNS-SD requirement must be an explicit optional feature choice for Lapis, not a reason to change global service policy.

## Prototype method actually used here

No install.sh, SteamOS release installer, migration script, or package install was executed. The experiment used native CMake plus temporary extracted Debian headers/pkgconf; it copied executables into the workspace and then explicitly ran tests. That is why no persistent installer changes were found to remove. Nevertheless, device tests DID send query bytes and manipulate serial settings/control lines. Being a local build did not make those interactions non-destructive by definition.

## Retain / replace

Retain: pinned source and submodules, private binary grouping, checksums, honest capability-specific hardware evidence, license/corresponding-source handling, explicit legacy-conflict detection.

Replace for Lapis: x86-only packaging, Distrobox build assumption, shared user-service ownership, install=enable/start behavior, install-time readiness claims, broad upstream tty discovery, inherited serial settings, infinite handshake waits and unrestricted default OSC binds.

Do not import a migration/uninstall script until all removal/stop paths are reviewed against Lapis ownership. Never repurpose Lapis gadget/MTP components as a SerialOSC dependency to be disabled. See INTEGRATION.md for acceptance gates and lifecycle design.
