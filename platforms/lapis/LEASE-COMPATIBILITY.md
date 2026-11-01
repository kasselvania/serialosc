# Lease / release compatibility and provenance

## Where the work is

Server repository: https://github.com/kasselvania/serialosc/tree/7187832c349202b1a94a9b10080ae57d40069946

- feature/leased-destinations at 7187832c349202b1a94a9b10080ae57d40069946.
- Main c96ea389dbf82c84d17f6f7adddaf311aed49438 does not include it.
- This Lapis integration branch descends from 7187832, so it contains the same server lease implementation and Mac detector/write-return corrections.
- Specification: docs/leased-destinations.md in this source tree.

Client repository: https://github.com/kasselvania/PlugData-Monome-Devices/tree/09c820e751525903ec61da00c5be1cb0ccd63a2b

- feature/serialosc-leases at 09c820e751525903ec61da00c5be1cb0ccd63a2b.
- The lease feature is not established merely by using the client repository's main branch.
- Relevant evidence: docs/MACOS-LEASE-CANDIDATE.md, docs/LEASE-WORKBENCH.md, docs/STEAMOS-LEASE-CANDIDATE.md; Mac manager tools/macos_serialosc_lease_candidate.sh; lease smoke patch and tests.

## Included server semantics

Lease v1 is opt-in and per-device, not in the supervisor. It provides capability/state, acquire, explicit takeover of a legacy destination, renew and exact-token release. Requests address the per-device OSC port, not discovery port 12002. Client policy is TTL 6000 ms renewed every 2000 ms; daemon validates 1000–60000 ms, not a hidden fixed six-second rule.

The worker uses monotonic deadlines and clears expired callbacks even with no incoming traffic. Release and expiry attempt capability-aware darkening and move the runtime destination to free port 0. A new lease requires a grant before output. Runtime leased destinations are not restored as live callbacks after reconnect; persistence saves a free callback. Legacy destination setters can still displace a lease. Tokens prevent accidental cross-session operations, not malicious traffic; normal LED commands are not token-authenticated. This is not a security lock.

No claim that release recreates a prior visual state: it darkens output. Hardware errors can prevent darkening even when the stale callback has been cleared; independent state verification matters. Immediate shutdown hooks alone cannot guarantee cleanup after SIGKILL/host death; expiry in a still-running, healthy device worker is the fallback. If the Lapis/worker itself dies, the lease cannot promise physical darkening.

## Mac evidence versus Lapis proof

Client docs record Mac standalone acceptance for legacy and Zero grids, the four-ring Arc, simultaneous three devices, and a bounded Bitwig shared-host process-death slice against server 7187832 and the specified PlugData candidates. They include exact callback isolation, explicit selection, renewal, orderly release, disconnect/reconnect and expiry observations. These are repository evidence claims, not newly reproduced here. They do not transfer hardware acceptance to ARM64 Linux merely because the same source is portable.

Current Lapis verification:

- Base source ancestry includes lease implementation and all four existing C test sources.
- Pure lease core compiled/run on Debian 13 aarch64 in the hardware-free Lapis target.
- Passive doctor passed three synthetic-filesystem tests.
- Full liblo runtime/OSC/event-loop tests have NOT been run here for this candidate.
- No full Lapis daemon binary was built or activated for this candidate.
- No PlugData client/Bitwig lifecycle or physical Arc lease test was performed on Lapis.
- Abnormal Arc startup remains a blocking gate, not fixed by lease support.

The retired Lapis main-based prototype had none of the lease changes. Do not identify it with the Mac candidate. See INTEGRATION.md before any active build/installation.
