# Inert strict candidate launcher

**Not an installer or hardware-readiness claim.** Python 3.11+ and Linux procfs
are required for activation. Default invocation only reads local profile,
manifest, hashes and existing state metadata. It never creates directories,
spawns processes, binds ports, opens devices or runs version probes.

## Exact profile and environment

Supply a JSON object with exactly these string fields (synthetic example):

    {"vid":"0403","pid":"6001","serial":"EXACT-SERIAL","interface":"00","surface":"arc2"}

VID/PID are exactly four hexadecimal digits; interface is exactly two. No 0x
prefixes, integers, wildcards, serial inference or omitted defaults. Serial is
1–128 ASCII letters, digits, dot, underscore or hyphen (not dot or dot-dot).
Surface is explicitly arc2, arc4 or grid, independent of serial spelling.
Duplicate and extra JSON keys are rejected.

The child environment allowlist contains exactly these settings; ambient HOME,
XDG, PATH, loader injection and existing USB/surface settings are not inherited:

    LAPIS_SERIALOSC_STRICT=1
    LAPIS_SERIALOSC_USB_VID=<vid>
    LAPIS_SERIALOSC_USB_PID=<pid>
    LAPIS_SERIALOSC_USB_SERIAL=<serial>
    LAPIS_SERIALOSC_USB_INTERFACE=<interface>
    LAPIS_SERIALOSC_SURFACES=<serial>:<surface>

These full LAPIS_SERIALOSC_USB_* names match the C policy implementation.

## Build manifest contract

The existing build's explicit --bin-dir must contain the three executable
regular files and lapis-candidate.json:

    {
      "schema": 1,
      "strict": true,
      "source_commit": "<full lowercase 40- or 64-digit commit hash>",
      "files": {
        "serialoscd": "<lowercase SHA-256>",
        "serialosc-device": "<lowercase SHA-256>",
        "serialosc-detector": "<lowercase SHA-256>"
      }
    }

No additional keys are accepted. Build generation must emit this only for a
strict build after binaries are complete. All hashes are checked, again after
activation locks are acquired. Files must be owned by the current user,
single-link regular files, not symlinks, and not group/world writable; binaries
must be executable. Profile and manifest files have the same ownership/write
policy. Adjust permissions on **your own project artifacts** if necessary.

The manifest is **unsigned, owner-controlled provenance**, not proof of compiler
flags, code signing, trust or hardware safety. A cooperating owner can change
both hashes and code. Do not rebuild/replace files while activating. Dynamic
libraries are not covered by this three-file manifest. There is no privileged
installer or tamper-proof sandbox.

## Read-only validation

From checkout root, using your own explicitly prepared profile and build path:

    python3 platforms/lapis/run_candidate.py --profile ./candidate-profile.json --bin-dir ./YOUR-BUILD/bin --state-dir ./candidate-state

Bin and state paths must be inside this checkout, owned by the current user,
without symlinked components. They must be separate non-nested trees; the state
parent must already exist. The state directory itself need not exist. There is
no fallback to HOME or globally installed daemons. Do not use system build paths
or a shared state/config tree.

## Deliberate foreground activation (not performed by tests)

Only a deliberate invocation with **both --activate and --acknowledge-device-io**
starts the supervisor. Giving just one is an error. Activation can bind sockets,
open the allowlisted USB serial interface and send device initialization/output.
Successful validation or tests do not establish hardware readiness.

The launcher runs BIN/serialoscd --config-dir STATE/config in a new child
session/process group, with stdin closed and stdout/stderr attached to its
foreground caller. It does not detach itself or install services. Stop with
Ctrl-C/SIGINT or SIGTERM to the launcher; no name-based stop command is provided.

Only activation creates STATE and STATE/config, owned and mode 0700. Existing
directories must already satisfy that policy. Config files must be regular,
owner-only mode 0600. Existing device config contents are never reset, removed
or silently replaced by the launcher. Child umask is 077. Activation atomically
saves mode-0600 profile.json, candidate-manifest.json and receipt.json using
same-directory temporary files, fsync and rename. Receipts record provenance
and process outcome, not startup readiness or successful physical blackout.

Persistent advisory flock locks protect the state inode and all three executable
inodes, including a different state directory using the same build. Lock files
are never unlinked; persisted receipt PIDs are never trusted for signalling.
Independently copied builds are not globally locked: there is no system-wide
daemon lookup or device-ownership service.

On SIGINT/TERM the launcher forwards the first signal to its own supervisor,
allowing orderly shutdown for --grace seconds (default 3). Remaining members of
only that child group then receive SIGTERM, wait --term-grace (default 1), then
SIGKILL as a last resort and one final term-grace observation. Durations must
be finite, 0.05–30 seconds. On spontaneous supervisor exit, remaining members
receive SIGINT before the same escalation. The leader stays unreaped until
cleanup completes so its group identifier cannot be recycled while used for
signalling. No process is killed by generic name or persisted PID/pidfile;
unrelated process groups are untouched.

This bounds userspace waiting, **not uninterruptible kernel USB/serial close**.
If members remain, the launcher returns failure and records cleanup-incomplete.
A starting, running or cleanup-incomplete receipt blocks another activation
pending manual inspection. After a crash, inspect the actual owned child
session and hardware condition yourself; never kill a PID merely because an old
receipt lists it. Once absence of that session is independently confirmed,
archive the stale receipt in your project before retrying. No automatic stale
process killing or config removal occurs.

This is a foreground cooperative lifecycle, not a cgroup supervisor: SIGKILL of
the launcher, descendants deliberately escaping its group/session, malicious
same-user edits, machine crashes and kernel hangs are outside its guarantees.
There are no HOME paths, services, udev edits, sysfs writes, USB rebinds or
system-wide changes.

## Hardware-free tests

    python3 -m unittest discover -s platforms/lapis/tests -p test_candidate_launcher.py -v

Tests copy the launcher into a temporary checkout layout under /tmp and create
only Python dummy executables, private state and fake manifests. They exercise
inert validation, exact profile/environment handling, hash/symlink rejection,
locks, config preservation, receipts, natural/nonzero exits, SIGINT/TERM,
stubborn descendants and scoped SIGKILL escalation with an unrelated child
left alive. No real daemon, serial port, USB device, socket or OSC endpoint is
used. Temporary files are cleaned by the tests. Build/CMake integration is
maintained separately.
