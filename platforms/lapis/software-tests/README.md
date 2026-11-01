# Hardware-free implementation checks

This separate project builds production lease/config/runtime/sys/lease OSC code
with test-owned monome callbacks, never libmonome serial platform code or a USB
detector/daemon entrypoint. Config tests use private temporary directories;
the default-preference resolver is deliberately trapped. Event-loop input is a
pipe. OSC/client tests use ephemeral local sockets, never discovery UDP12002.

    cmake -S platforms/lapis/software-tests -B /tmp/lapis-tests-release -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/lapis-tests-release --parallel 2
    ctest --test-dir /tmp/lapis-tests-release --output-on-failure

Repeat with Debug and with -DSOSC_LAPIS_STRICT=OFF. Strict is ON by default in
this test-only project, OFF by default in the actual top-level daemon project.
Strict tests patch clean private dependency copies; no source submodule edits.

Requires populated liblo/libmonome headers/source submodules, CMake 3.27+, C17
compiler, pthreads, patch, Python3.11+; no libudev development files, root, tty
permission, PlugData or physical device. Python launcher tests run only dummy
processes in their own groups and temporary checkout. They intentionally test
signals including scoped forced termination, not actual USB shutdown.

Coverage includes:
- Debug/Release checks plus deliberate false-check verifier.
- Lease core/runtime and real loopback OSC; poll and select timer expiry.
- Explicit surface family/count and exact cleanup-index boundaries.
- Real atomic config write, fault injection, pre/post-rename SIGKILL restart,
  malformed/unreadable config, preserved authority on failed grant.
- Exact allowlist parsing plus bounded synthetic sysfs ancestor validation.
- Actual loopback socket binding/local OSC/nonloopback-address rejection.
- Patched mext/termios/read/write/poll with every hardware syscall replaced.
- Workbench fake clock/UDP server lifecycle and protocol/source/type failures.
- Actual production lease/sys/runtime + workbench interoperability fixture
  with in-memory Arc2 output, temporary real config persistence and real UDP.
- Launcher exact schema/hash/ownership/default inertness/own-group lifecycle.
- Generated manifest accepted by actual validator; wrong patch baseline refused.

The fixture is not a real device worker: its ring output is in-memory and it
cannot validate libmonome ABI/device behavior. SIGKILL is not a power cut;
fsync tests do not certify storage power-loss durability. Linux default-policy
coverage is not a native Mac/Windows build or physical compatibility pass.

All physical startup/LED/encoder/reconnect, Lapis host shutdown, PlugData/DAW
and electrical tests remain separately required. See ../SOFTWARE-EVIDENCE.md
and ../ACCEPTANCE-GATES.md. Compiling the full daemon is a separate lane and
never implies permission to execute it.
