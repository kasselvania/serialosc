# Software-only supervisor shutdown fixture

This builds the production supervisor implementation via
test wrapper `tests/supervisor_fixture.c`, not a production installation.
The wrapper forces an **ephemeral 127.0.0.1 UDP bind**, verifies the event loop
closes cleanly, and can inject a deterministic signal callback before the
initial enable transition completes. Child paths resolve only to executable
copies of ``fake_child.py`` beside the fixture. The Python test refuses a production
supervisor filename or non-fixture child contents. No detector implementation,
libudev, libmonome implementation, serial backend, or worker daemon is linked.
Surface hardware functions are fatal stubs.

Run independently from the source root:

```sh
cmake -S platforms/lapis/supervisor-tests -B /tmp/lapis-supervisor-shutdown -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/lapis-supervisor-shutdown -j4
ctest --test-dir /tmp/lapis-supervisor-shutdown --output-on-failure
```

Repeat with ``-DCMAKE_BUILD_TYPE=Release`` in a separate build directory.
Alternatively run ``test_shutdown.py --fixture /absolute/fixture-bin/lapis-supervisor-fixture``
or supply the same path in ``LAPIS_SUPERVISOR_FIXTURE``. Both fake sibling children
must be copied and executable before running.

Tests verify SIGINT/SIGTERM worker should-exit IPC, asynchronous worker cleanup
before supervisor return, child reaping, no respawn on repeated signals or OSC
enable, rejection of a connection queued by the detector during termination,
signal races with pending enable and pending/completed OSC disable, a no-worker
shutdown, and retention of a deliberately stuck worker for launcher fallback.
The test's emergency cleanup kills **only its own isolated fixture process
group**; it never signals an existing serialosc service.

Fake worker cleanup is a software ordering marker, **not proof of hardware LED
darkening or physical I/O completion**. The supervisor intentionally has no
internal deadline or signal escalation. The launcher owns a bounded grace
period followed by process-group cleanup if a child does not exit. No restart
is attempted.
