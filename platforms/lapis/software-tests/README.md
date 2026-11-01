# Software protocol test harness — NO hardware acceptance

This isolated CMake project links the bundled liblo and production lease/runtime/event-loop code with test-owned monome functions. It does NOT link libmonome serial platform code or build/run a USB detector or daemon. Event-loop device input uses pipes, darkening uses counters. OSC tests bind ephemeral loopback sockets; this is active software protocol testing, not a purely passive check.

From repository root, repeat for Debug and Release:

    cmake -S platforms/lapis/software-tests -B /tmp/lapis-tests-Debug -DCMAKE_BUILD_TYPE=Debug
    cmake --build /tmp/lapis-tests-Debug --parallel 2
    ctest --test-dir /tmp/lapis-tests-Debug --output-on-failure

    cmake -S platforms/lapis/software-tests -B /tmp/lapis-tests-Release -DCMAKE_BUILD_TYPE=Release
    cmake --build /tmp/lapis-tests-Release --parallel 2
    ctest --test-dir /tmp/lapis-tests-Release --output-on-failure

Requires populated liblo/libmonome source submodules for headers and liblo source, C17 compiler, pthreads, CMake 3.27+. Does not require libudev development files, serial node access or root. Third-party CMake may emit deprecation warnings. The suite has a failing-check wrapper: it passes only if its child deliberately exits nonzero with the expected diagnostic. A negative self-test passing means assertion enforcement worked, not that a defect was ignored.

Passed locally on Debian 13 aarch64 with GCC 14.2 and CMake 3.31.6, Debug and Release (-O3 -DNDEBUG): runtime, loopback OSC, poll expiry, select expiry, check self-test and deliberate-negative-check verifier (six CTest cases per build). Pure lease core also passes as a separate optimized test; passive synthetic filesystem checks are in the parent offline target.

This receipt DOES NOT establish full daemon build, crash-durable persistence, physical ring-count correctness, startup animation, output voltage/current, encoder I/O, actual PlugData scheduling or safe Lapis USB lifecycle. See ../ACCEPTANCE-GATES.md. No hardware-ready release claim is warranted.
