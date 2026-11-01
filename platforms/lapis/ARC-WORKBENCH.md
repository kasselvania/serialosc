# Standalone Lapis Arc workbench

A dependency-free Python 3 CLI, independent of PlugData. It operates **one
explicit serial and one explicit per-device UDP endpoint**. It never opens
serial/USB, launches/reconfigures a daemon, scans ports, broadcasts, subscribes
to discovery, enumerates devices, or automatically claims a destination.

## Status and evidence boundary

Software implementation with offline tests, including a fake UDP server bound
to an ephemeral loopback port. **No production daemon, UDP 12002, serial/USB,
physical Arc, suspend/resume, reconnect, systemd or Lapis power-cycle acceptance
has been exercised by these tests.** A verified free lease is a protocol
readback, not independent proof that physical LEDs are dark.

The user must supply the physically known `arc2` or `arc4` profile. OSC v1
lease state and ordinary `/sys/info` do not advertise a reliable Arc ring
count. A zero/absent size does **not** establish Arc identity; a positive size
contradicts the supplied profile and is rejected. Probe output explicitly marks
`profile_verified: false`. This is not automatic hardware capability detection.

## Offline default

From the repository root:

```sh
python3 platforms/lapis/arc_workbench.py
python3 platforms/lapis/arc_workbench.py inspect
python3 platforms/lapis/arc_workbench.py --help
python3 platforms/lapis/arc_workbench.py run --help
```

All of these stay offline, with no socket creation. Argument validation also
runs before binding/transmitting. There is no default network command.

## Selected-device read-only probe

Obtain the serial and **per-device** UDP port through an operator-approved
external source. The workbench deliberately does not discover them. Substitute
real values; do not copy a discovery port into this command:

```sh
python3 platforms/lapis/arc_workbench.py probe \
  --serial YOUR-ARC-SERIAL --device-port 23456 --profile arc2
```

Only numeric loopback IPv4 addresses are accepted for the selected server and
callback; both default to `127.0.0.1`. There is no remote/network override,
DNS lookup, `0.0.0.0` binding, broadcast or multicast. The callback defaults
to a random free port (`--callback-port 0`); use an explicit callback port only
when needed. The server's reply **source address and port must exactly match**
the selected device endpoint. A daemon that does not support that contract is
incompatible; the client does not relax validation.

Probe sends only `/sys/info si` and token-qualified `/sys/lease/info ssi`.
It checks the serial identity, expected field types, conflicting fields, size
contradictions and lease version-1 `isssisii` state invariants. It does not write
legacy settings. Unsupported/unreachable/dropped replies produce
`NO_REPLY_UNKNOWN` and a nonzero exit status, never “zero devices.” SerialOSC's
list protocol has no end marker; silence cannot prove an empty inventory.
There is intentionally no device-count result or fabricated “zero” state.

## Bounded output, encoder receive, renew and release

```sh
python3 platforms/lapis/arc_workbench.py run \
  --serial YOUR-ARC-SERIAL --device-port 23456 --profile arc2 \
  --acquire --ring 0 --level 4 --seconds 5
```

- `--acquire` is mandatory for every mutating invocation. There is no
  auto-claim-on-probe or fallback to legacy mode.
- A free destination is acquired with a new random token and prefix. An active
  different lease is refused, including when takeover was requested.
- Legacy mode is refused unless the command also includes
  `--takeover-legacy`. This uses the lease takeover operation, **not** legacy
  host/port/prefix writes. Release leaves the device free, not restored to the
  previous legacy destination. Review that consequence before takeover.
- A matching-token, matching-TTL grant is required, followed by owner, serial,
  destination host/port and prefix readback. Only then is one `/ring/all ii`
  command sent for the selected ring. Ring is zero-based; level is 0–15.
- The explicit profile bounds ring indices to 0–1 or 0–3. CLI sends exactly
  one ring pattern; the reusable API limits output to 64 packets/session.
- The listener prints at most 200 validated encoder delta/key events. Only the
  exact session prefix, `ii` types, valid ring, and key state 0/1 are accepted.
  Delta remains a signed int32 (it is not artificially limited to ±1).
- Duration is 0–30 seconds. TTL is 1000–10000 ms (default 3000). Renewal runs
  every TTL/3, requires matching acknowledgements and subsequent ownership
  readback, and is rejected locally if acknowledgement is late relative to the
  previous conservative deadline. A missing acknowledgement disables output;
  there is no retry-acquire that could resurrect a lost session.
- The timeout is 0.01–1 second (default 0.25); the TTL must leave a two-timeout
  plus 100 ms margin. Deadlines use monotonic time. Receive loops have packet
  budgets to bound floods; the client is not a real-time scheduler.
- Release is explicit in the `run` flow: re-prove ownership, send the
  token-checked release, require an appropriate acknowledgement, then verify
  mode free/port 0/owner 0. If the ACK was lost or the daemon rejected release
  (for example `device_error` after clearing its callback), independent free
  readback is authoritative and the ACK problem is printed. A wrong-token or
  malformed ACK is **not** accepted as success.

Normal completion and interruption attempt ownership-gated cleanup. Release is
itself the daemon's atomic token-checked darkening operation; this client does
not send unprotected “all off” messages or restore guessed settings. After
lease loss, a bad source, bad token, malformed packet, stale local deadline or
failed ownership readback it skips release/output rather than risk disturbing
another owner. Missing grant/claim readback is not ownership proof. An acquired
but unproved lease may therefore remain until its bounded daemon TTL expires.
Cleanup failure is reported, not presented as verified restoration.

## Explicit expiry experiment

This deliberately omits renew/release and asks the daemon to clean up by TTL:

```sh
python3 platforms/lapis/arc_workbench.py expiry-test \
  --serial YOUR-ARC-SERIAL --device-port 23456 --profile arc4 \
  --acquire --confirm-expiry-test --ttl-ms 3000 --ring 0 --level 4
```

The separate command and confirmation flag are both required. The experiment
waits only until the conservative client deadline plus one timeout. That
deadline starts before sending acquire; the daemon refreshes its TTL after
persistence, so a delayed grant does not extend the client's ownership window.
The experiment then requires mode free/port 0/owner 0 readback and reports
exactly the evidence observed:

- `EXPIRY_NOTIFICATION_AND_FREE`: a matching-token `lost expired` notification
  from the selected endpoint was received, followed by free-state readback.
- `FREE_AFTER_NO_RENEW`: free-state readback without that notification. The
  notification is best-effort, so this is a successful free-state observation,
  **not unique proof of timer cleanup**. An intervening legacy `/sys/port 0`
  (with its displacement notification dropped) produces the same observation.

There is no blanket expiry-verification claim. A wrong-token, malformed or
non-expiry loss notification still fails closed; non-free readback also fails.
Neither result proves physical darkness, nor authenticates the daemon or rules
out other loopback clients. The experiment never releases or sends LED/legacy
configuration writes after expiry/loss. Observe physical darkness independently
during hardware acceptance; the CLI prints that it has **not** observed it.

## Strictness and trust model

Only canonical OSC messages with integer/string atoms are supported. Bundles,
floats, wildcards, trailing data, truncation, nonzero padding, invalid UTF-8,
embedded NULs, coercions (including bool-to-int), oversize strings/packets and
unexpected protocol types are rejected. Limits: 4096-byte datagrams, 1024-byte
UTF-8 strings, 128 arguments. Reads use a limit+1 buffer to detect oversize
packets. All lease acknowledgements check path, tags, token and TTL. Fresh
cryptographic random session tokens/prefixes prevent accidental session reuse.

Lease tokens are **not authentication or authorization**. Loopback processes
can still spoof traffic or use legacy setters to displace the destination;
this protocol cannot eliminate the readback-to-output race or UDP loss/reorder.
Fail-closed behavior may terminate on delayed/reordered/duplicate replies.
There are no unsafe automatic retries, remote-access claims, LED delivery ACKs,
or claimed physical success. Killing the Python process cannot guarantee
client cleanup; server expiry is the bounded fallback and needs hardware and
host-lifecycle acceptance separately.

## Tests (offline only)

```sh
python3 -m unittest discover -s platforms/lapis/tests -p test_arc_workbench.py -v
```

Injected clock/transport tests exercise framing errors, source/token/TTL
mismatches, selected serial identity, ambiguous/grid/partial info, unsupported
lease silence, free/legacy/busy acquisition, takeover consent, owner readback,
output/profile/argument bounds, encoder filtering, loss/displacement,
renew rejection/timeout/late ACK/local expiry, explicit release, rejected or
missing release ACK plus free readback, failed release readback, unproved
cleanup, transport failures, expiry with/without loss notification, delayed
grants with distinct client/daemon deadlines, intervening legacy port-zero
state, and invalid/displacement notifications during the expiry experiment.
A fake UDP server binds `127.0.0.1:0` and exercises framing and full lifecycle
plus no-reply behavior through real sockets. It never contacts UDP 12002 or
production daemon/serial/USB. Tests cannot prove all network interleavings or
hardware behavior; they are regression coverage, not physical acceptance.

## Provenance and ownership

Adapted design/OSC framing concepts from:

- `/tmp/lapis-serialosc-review/test/osc_workbench.py`
  (SerialOSC physical workbench).
- `/tmp/lapis-lease-audit/devices-leases/tools/live_serialosc_lease.py`
  (lease-v1 probe, acquire, renew/release and expiry lifecycle).
- The version-1 wire contract in `docs/leased-destinations.md` in this source
  tree. It is a fork extension, not a claim about upstream SerialOSC support.

Those scratch paths are provenance, not runtime imports/dependencies. Original
copyright/permission attribution is retained in `arc_workbench.py`; see the
repository `COPYRIGHT` as well. This adaptation is standard-library-only and
includes no PlugData patches or new daemon code. Its owned source/doc files
are `platforms/lapis/arc_workbench.py`,
`platforms/lapis/tests/test_arc_workbench.py` and this README.
