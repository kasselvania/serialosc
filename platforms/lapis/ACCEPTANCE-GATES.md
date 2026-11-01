# Lapis acceptance gates: evidence, not substitutions

Software checks do NOT qualify as hardware acceptance. Compilation does NOT establish safe startup, power capacity, physical output/input, or Lapis lifecycle correctness.

## What is needed from the operator

Nothing for source fixes and offline checks. Do not reconnect the Arc just to make software progress. No credentials, broad root grant, global USB/MTP disabling or new hardware purchase is requested.

Later hardware work needs the operator to perform a controlled physical connection and report/record startup animation and exact requested LED/input effects. This agent has no camera feed of the Arc and cannot validate its behavior from enumeration or OSC acknowledgements alone. A video/camera observation or attended confirmation must accompany logs. Observe-only attachment should precede any application access.

Attachment-level tracing/target-only driver/probe isolation needs an administrator-approved route: current agent can read logs but cannot access raw USB/reset/driver-binding/power controls. There is no verified Lapis app switch granting that specific operation. A device administrator/platform support must supply an approved narrow method; no password or bypass should be requested. Agree on exact target, operation and rollback before executing anything.

Definitive electrical evidence, if the power hypothesis must be proven/disproven, requires independent VBUS/ramp/inrush measurement or an authoritative board specification and suitable instrumentation. Software flags alone do not prove voltage quality. If instrumentation is unavailable, record the electrical cause as UNDETERMINED, not passed. Human observation can establish functional boot, but not certify the underlying waveform.

## Gate matrix

| Gate | Required evidence | Present status |
| --- | --- | --- |
| Release assertions stay active | Optimized NDEBUG build runs checks, deliberate bad check exits nonzero, suites execute | Software work only; record exact run receipts |
| Client lease lifecycle | Acquire/renew/wrong-token/expiry/release/readback with typed protocol tests | Mock/loopback evidence only until actual client/device/host run |
| Abrupt worker persistence | Real config-file test, crash between grant and shutdown, restart from same state, no stale callback; durable error failure path | OPEN; source gap found, no hardware substitution |
| Arc capability boundary | Verified ring-count policy and negative unknown-device behavior, then actual target output test | OPEN; current four-index fallback is not a proof |
| Narrow device selection | No open for unallowlisted target; identity/reconnect race and unknown serial tests; actual open trace later | OPEN; design only |
| Bounded init and line behavior | Fault-injected silent/partial/error transport with deadline and exact calls; physical compatibility later | OPEN; design only |
| Network boundary | Actual listener ownership/binds and negative remote-reachability tests for supervisor and workers | OPEN; passive/offline target cannot establish it |
| Clean boot on Lapis | Timestamped physical video/observation plus attach logs with no application/probe interaction beyond documented host behavior | FAILED/UNRESOLVED: absent normal animation and abnormal LEDs observed |
| Lapis physical input/output | Exact serial/port, normal boot, known pattern per ring, encoder events, independent readback | NOT RUN for candidate |
| Hardware lease cleanup | Physical light persists immediately after client kill, then darkens at expiry, free state; orderly release separately | NOT RUN on Lapis |
| Host lifecycle/hotplug | Single/multi-device start/stop, reconnect/survivors, real PlugData/Bitwig lifecycle with exact pins | NOT RUN on Lapis |
| Electrical capacity/startup | Authoritative output rating, measured supply/transient behavior under declared load as needed | UNKNOWN; no output-current rating or waveform proof |

A passing software lane can close only the corresponding software claim. Hardware readiness requires the physical lanes above; do not average or roll them into a single green status. Crash tests use synthetic devices first; never deliberately fault real hardware for a code-only check.

## Evidence receipt requirements

Record source commit plus working-tree diff/hash if uncommitted, compiler/build type, exact commands, exit statuses and full logs. Distinguish fake serial/in-memory monome, pipe-based event loop, loopback UDP, real device, real host process, physical observation and electrical measurement. Keep raw host/user identifiers private unless explicitly approved. A report must state skipped/blocked/unknown alongside passes.

No release/installable binary or hardware-ready tag until hardware gates actually pass. Candidate/source commits remain useful while gates are open.
