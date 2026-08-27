# Leased application destinations

Status: implemented and automatically tested on the
`feature/leased-destinations` fork branch. Physical and host-lifecycle
acceptance is still pending. This document does not describe behavior in an
upstream SerialOSC release.

## Problem

A SerialOSC device server has one outgoing application host, port, and prefix.
The existing `/sys/host`, `/sys/port`, and `/sys/prefix` messages replace that
destination and the worker later saves it in the per-device configuration.

OSC uses UDP, so the device server receives no connection-close event when an
application crashes or a plug-in host is terminated. The worker can therefore
continue to target a dead callback port and the hardware can retain its last
LED state indefinitely.

Leased destinations add bounded liveness to that application destination. The
device worker already outlives a terminated client, so it is the component
that can safely darken the hardware and clear an expired callback.

## Compatibility invariants

1. Lease behavior is opt-in. A client that sends no `/sys/lease/*` messages
   receives existing SerialOSC behavior.
2. Existing `/sys/host`, `/sys/port`, `/sys/prefix`, `/sys/info`, device OSC,
   discovery, and configuration formats remain valid.
3. An active lease refuses another lease token until it expires or is
   released. A legacy `/sys/host`, `/sys/port`, or `/sys/prefix` write retains
   its historical last-writer behavior and explicitly displaces the lease.
4. A lease token prevents accidental cross-session renew and release. It is
   not authentication, access control, or protection against a malicious OSC
   sender.
5. Lease host, port, and prefix are runtime state and are never persisted as a
   callback to restore after worker restart or physical-device reconnect.
6. A client must receive a grant before sending capability output. A timeout
   is not a grant.

## Terms

- **free**: runtime destination port is `0` and there is no active lease.
- **legacy**: the destination is managed by the existing `/sys/*` messages and
  persistence behavior.
- **leased**: one session token owns the runtime destination until release,
  displacement, or expiry.
- **token**: a non-empty opaque OSC string generated for one client lifetime.
  Version 1 limits it to 128 UTF-8 bytes. Tokens should be unguessable enough
  to avoid accidental collision, but they are not security credentials.
- **TTL**: requested lease lifetime in milliseconds. Version 1 accepts values
  from `1000` through `60000` inclusive and rejects values outside that range.

The initial PlugData policy is a `6000` ms TTL renewed every `2000` ms. That is
a client policy, not a hidden daemon default, and remains subject to DAW-host
acceptance.

## Version 1 wire protocol

All messages go to the selected per-device SerialOSC server, not discovery
port `12002`.

Version 1 OSC type tags are fixed:

| Path | Type tag |
| --- | --- |
| `/sys/lease/info` | `si` or `ssi` |
| `/sys/lease/state` | `isssisii` |
| `/sys/lease/acquire` | `ssisi` |
| `/sys/lease/takeover` | `ssisi` |
| `/sys/lease/renew` | `sisi` |
| `/sys/lease/release` | `ssi` |
| `/sys/lease/granted` | `si` |
| `/sys/lease/renewed` | `si` |
| `/sys/lease/released` | `s` |
| `/sys/lease/rejected` | `ss` |
| `/sys/lease/lost` | `ss` |

### Capability and state query

```text
/sys/lease/info <reply-host:string> <reply-port:int>
/sys/lease/info <token:string> <reply-host:string> <reply-port:int>
```

A lease-capable server replies to the explicit callback:

```text
/sys/lease/state <version:int> <serial:string> <mode:string>
                 <host:string> <port:int> <prefix:string>
                 <remaining-ms:int> <owner:int>
```

Version is `1`. Mode is `free`, `legacy`, or `leased`. `remaining-ms` is `0`
outside leased mode. `owner` is `1` only when the optional query token matches
the active token; the server never exposes the active token. No response means
unsupported or unreachable and must fail closed unless the user explicitly
chooses legacy mode.

### Acquire a free destination

```text
/sys/lease/acquire <token:string> <host:string> <port:int>
                   <prefix:string> <ttl-ms:int>
```

The request succeeds only when the device is free, the prior lease has already
expired, or the same token repeats the identical destination and prefix. The
same-token form is an idempotent retry and renews the deadline. Reusing a token
with different settings is rejected.

### Explicitly replace a legacy destination

```text
/sys/lease/takeover <token:string> <host:string> <port:int>
                    <prefix:string> <ttl-ms:int>
```

This has the same validation as `acquire` but may replace a non-zero legacy
destination. It never replaces a different active, unexpired lease. A client
must expose this as an explicit takeover decision; probing a device is not
permission to take it over.

### Renew

```text
/sys/lease/renew <token:string> <ttl-ms:int>
                 <reply-host:string> <reply-port:int>
```

Only the matching active token can renew. The new monotonic deadline is the
time the server accepts the request plus the supplied TTL. A late renewal is
rejected after expiry cleanup; it cannot resurrect the old session. The
explicit reply target lets the server report `no_lease` after the old runtime
destination has already been cleared.

### Release

```text
/sys/lease/release <token:string> <reply-host:string> <reply-port:int>
```

Only the matching active token can release. Release darkens the supported
Grid or Arc output surface, changes the runtime destination to port `0`, and
clears the lease. Repeating release after that transition is rejected as
`no_lease`; clients should treat an independently verified free state as the
idempotent success condition.

### Replies

Replies to `info`, `acquire`, `takeover`, `renew`, and `release` go to the
explicit callback carried by that request. Only the best-effort `lost`
notification uses the captured leased destination:

```text
/sys/lease/granted  <token:string> <ttl-ms:int>
/sys/lease/renewed  <token:string> <ttl-ms:int>
/sys/lease/released <token:string>
/sys/lease/rejected <token:string> <reason:string>
/sys/lease/lost     <token:string> <reason:string>
```

Version 1 rejection reasons are:

- `invalid_request`
- `busy`
- `legacy_destination`
- `claim_mismatch`
- `not_owner`
- `no_lease`
- `device_error`
- `server_error`

`device_error` means the worker could not complete the requested hardware
output reset. `server_error` means it could not allocate the runtime state
needed for the transition. Neither result is a grant. A release may report
`device_error` after it has still cleared the dead callback; clients must query
state rather than interpreting that error as continued ownership.

`lost` is a best-effort notification sent to the old leased destination before
a legacy write displaces it or before expiry clears it. Correct cleanup cannot
depend on the dead client receiving that message.

## State transitions

```text
free --acquire/grant------------------------------> leased
legacy --takeover/grant---------------------------> leased
leased --matching renew---------------------------> leased
leased --matching release/darken------------------> free
leased --deadline/darken--------------------------> free
leased --legacy destination write-----------------> legacy
leased --different token--------------------------> leased + reject requester
```

Before granting a new lease, the server darkens the supported output surface
so a new session does not inherit the previous application's display. The
grant is sent only after this reset and the atomic destination change.

A legacy write that displaces an active lease does not add new darkening or
other output behavior to the legacy protocol. It sends `lost` best-effort,
clears lease metadata, and then performs the existing operation.

## Expiry and persistence

Deadlines use elapsed monotonic time and are unaffected by wall-clock changes.
Every event-loop implementation must wake no later than the active deadline,
even when there is no device, OSC, or supervisor traffic.

On expiry, the worker:

1. captures the old leased destination;
2. darkens the supported Grid or Arc output surface;
3. emits best-effort `lost ... expired` to the captured destination;
4. changes the runtime destination to port `0`; and
5. destroys the token and deadline.

The leased host, port, and prefix never replace the saved legacy callback. If
the worker exits or the physical device disconnects while leased, its saved
configuration must contain destination port `0`; reconnect therefore starts
free instead of restoring a dead callback. Host and prefix may retain the
pre-lease legacy values, but they have no callback authority while the saved
port is `0`.

## Scope of ownership

The lease atomically controls SerialOSC's outgoing application destination and
its cleanup lifetime. Version 1 does not attach a token to every Grid or Arc
LED command, so it is not an authenticated hardware lock. Existing clients
and diagnostic tools can still send normal device OSC, and legacy destination
writes retain compatibility precedence.

PlugData must continue to perform exact readback and report displacement. The
lease closes the dead-client cleanup gap; it does not justify silently taking
another application's device.

## Implementation

The fork implementation includes:

- lease state in `sosc_state_t` separate from persisted legacy state;
- validation and atomic transition helpers shared by OSC handlers and timers;
- a monotonic clock and bounded timeout in the POSIX `poll` and `select`
  loops;
- a race-safe timeout path in the Windows event-loop implementation;
- Grid and Arc capability-aware all-dark cleanup;
- port-`0`-safe `/sys/info` and configuration writes; and
- logging that identifies transitions without logging lease tokens.

No lease logic belongs in `serialoscd`: each `serialosc-device` worker owns one
physical device, its outgoing destination, and its expiry.

The CMake test suite currently proves the timer core, runtime state and
persistence boundary, real loopback OSC messages, legacy displacement, safe
port-`0` readback, Grid/four-index Arc darkening, and deadline wakeup without
incoming traffic:

```sh
cmake -S . -B build -Dbuild_with_zeroconf=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

These tests are not physical-device or DAW-host acceptance.

## Acceptance matrix

Automated protocol tests must prove:

- old clients observe unchanged `/sys/*` behavior;
- capability query and explicit unsupported timeout;
- acquire from free and reject from legacy without takeover;
- explicit legacy takeover;
- same-token idempotent acquire and mismatched-token rejection;
- renew before deadline and rejection after deadline;
- exact-token release and wrong-token refusal;
- expiry with no incoming traffic;
- dark-before-free ordering for Grid and two- and four-ring Arc;
- legacy displacement of a lease without changing legacy output semantics;
- port `0` readback without a null dereference;
- physical disconnect while leased persists port `0`; and
- worker restart never restores the leased callback.

Platform acceptance must then cover:

- PlugData standalone normal release and forced process termination;
- Bitwig CLAP with stopped transport, ordinary bypass, save/reload, editor
  close/reopen, full device deactivation, and isolated host crash;
- a legacy 128 Grid, a 256 Grid, a four-ring Arc, pairs, and all three devices;
- hot unplug/reconnect and survivor continuity;
- an unmodified legacy client such as the maintained Monome Max package; and
- the Steam Deck rootless service and immutable-host installer boundary.
