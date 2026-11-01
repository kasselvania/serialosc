cmake_minimum_required(VERSION 3.27)
project(serialosc_lapis_mocked_protocol_tests LANGUAGES C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
get_filename_component(SOSC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
enable_testing()
set(WITH_STATIC ON)
set(WITH_TESTS OFF)
set(WITH_CPP_TESTS OFF)
set(WITH_TOOLS OFF)
set(WITH_EXAMPLES OFF)
option(SOSC_LAPIS_STRICT "Exercise strict Lapis software policy" ON)
set(SOSC_LO "${SOSC_ROOT}/third-party/liblo")
include("${SOSC_ROOT}/platforms/lapis/cmake/PrivateDependencies.cmake")
if(SOSC_LAPIS_STRICT)
    add_compile_definitions(SOSC_LAPIS_STRICT SOSC_LAPIS_LOOPBACK_ONLY)
    lapis_patched_dependency(liblo "${SOSC_LO}"
        "${SOSC_ROOT}/platforms/lapis/patches/liblo-explicit-unicast-bind.patch" SOSC_LO)
endif()
add_subdirectory("${SOSC_LO}/cmake" liblo EXCLUDE_FROM_ALL)
add_subdirectory("${SOSC_ROOT}/third-party/confuse" confuse EXCLUDE_FROM_ALL)
find_package(Threads REQUIRED)
add_library(lapis_test_common OBJECT
    "${SOSC_ROOT}/src/common/ipc.c"
    "${SOSC_ROOT}/src/common/util.c"
    "${SOSC_ROOT}/src/common/platform/posix.c")
target_include_directories(lapis_test_common PUBLIC
    "${SOSC_ROOT}/include" "${SOSC_ROOT}/third-party/libmonome/public"
    "${SOSC_LO}" "${CMAKE_CURRENT_BINARY_DIR}/liblo")
target_compile_definitions(lapis_test_common PRIVATE _GNU_SOURCE)

function(add_mocked_lease_test name file)
    add_executable(${name}
        "${SOSC_ROOT}/tests/${file}"
        "${SOSC_ROOT}/src/serialosc-device/lease.c"
        "${SOSC_ROOT}/src/serialosc-device/lease_runtime.c"
        "${SOSC_ROOT}/src/serialosc-device/surface.c"
        ${ARGN})
    target_link_libraries(${name} PRIVATE lapis_test_common liblo_static Threads::Threads)
    target_compile_definitions(${name} PRIVATE _GNU_SOURCE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 15)
endfunction()
add_mocked_lease_test(lapis-lease-runtime lease_runtime_test.c)
add_mocked_lease_test(lapis-lease-osc lease_osc_test.c
    "${SOSC_ROOT}/src/serialosc-device/osc/lease_methods.c"
    "${SOSC_ROOT}/src/serialosc-device/osc/sys_methods.c")
add_mocked_lease_test(lapis-lease-poll lease_event_loop_test.c
    "${SOSC_ROOT}/src/serialosc-device/event_loop/poll.c")
add_mocked_lease_test(lapis-lease-select lease_event_loop_test.c
    "${SOSC_ROOT}/src/serialosc-device/event_loop/select.c")

# No libmonome target, USB detector, daemon, or serial platform code is linked.
# Existing tests substitute monome calls with in-memory counters and pipes.
# OSC tests use ephemeral loopback sockets: software protocol traffic, not USB.

add_executable(lapis-check-self-test "${SOSC_ROOT}/tests/test_check_test.c")
add_test(NAME lapis-check-pass COMMAND lapis-check-self-test)
add_test(NAME lapis-check-intentional-failure
    COMMAND "${CMAKE_COMMAND}"
        "-DTEST_PROGRAM=$<TARGET_FILE:lapis-check-self-test>"
        -P "${SOSC_ROOT}/tests/test_check_failure.cmake")

add_executable(lapis-surface-test "${SOSC_ROOT}/tests/surface_test.c" "${SOSC_ROOT}/src/serialosc-device/surface.c")
target_link_libraries(lapis-surface-test PRIVATE lapis_test_common liblo_static)
target_include_directories(lapis-surface-test PRIVATE "${SOSC_ROOT}/include" "${SOSC_ROOT}/third-party/libmonome/public")
add_test(NAME lapis-surface-test COMMAND lapis-surface-test)

add_mocked_lease_test(lapis-surface-runtime surface_runtime_test.c)
add_mocked_lease_test(lapis-persistence lease_persistence_test.c
    "${SOSC_ROOT}/src/serialosc-device/config.c"
    "${SOSC_ROOT}/src/serialosc-device/config_persistence.c")
target_link_libraries(lapis-persistence PRIVATE confuse)
add_executable(lapis-usb-policy "${SOSC_ROOT}/tests/lapis_usb_policy_test.c"
    "${SOSC_ROOT}/src/common/lapis_usb_policy.c")
target_include_directories(lapis-usb-policy PRIVATE "${SOSC_ROOT}/include")
add_test(NAME lapis-usb-policy COMMAND lapis-usb-policy)
add_executable(lapis-network "${SOSC_ROOT}/tests/network_test.c" "${SOSC_ROOT}/src/common/network.c")
target_link_libraries(lapis-network PRIVATE lapis_test_common liblo_static)
add_test(NAME lapis-network COMMAND lapis-network)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME lapis-python-tests COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${SOSC_ROOT}/platforms/lapis/tests" -v)
set_tests_properties(lapis-python-tests PROPERTIES TIMEOUT 45 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

add_library(lapis-usb-policy-fixture SHARED "${SOSC_ROOT}/src/common/lapis_usb_policy.c" "${SOSC_ROOT}/src/common/lapis_usb_linux.c")
target_include_directories(lapis-usb-policy-fixture PRIVATE "${SOSC_ROOT}/include")
set_tests_properties(lapis-python-tests PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;LAPIS_USB_POLICY_LIBRARY=$<TARGET_FILE:lapis-usb-policy-fixture>")
if(SOSC_LAPIS_STRICT)
    lapis_patched_dependency(libmonome "${SOSC_ROOT}/third-party/libmonome"
        "${SOSC_ROOT}/platforms/lapis/patches/libmonome-bounded-mext.patch" SOSC_MONOME)
    foreach(kind mext posix_io linux_poll)
        add_executable(lapis-${kind}-fake "${SOSC_ROOT}/platforms/lapis/tests/${kind}_fake_test.c")
        target_compile_definitions(lapis-${kind}-fake PRIVATE LAPIS_SERIALOSC_BOUNDED_MEXT EMBED_PROTOS)
        target_include_directories(lapis-${kind}-fake PRIVATE "${SOSC_MONOME}/src/proto" "${SOSC_MONOME}/src/platform" "${SOSC_MONOME}/src/private" "${SOSC_MONOME}/public")
        target_compile_options(lapis-${kind}-fake PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(lapis-${kind}-fake PRIVATE -Wl,--gc-sections)
        add_test(NAME lapis-${kind}-fake COMMAND lapis-${kind}-fake)
        set_tests_properties(lapis-${kind}-fake PROPERTIES TIMEOUT 10)
    endforeach()
endif()

if(SOSC_LAPIS_STRICT)
    add_executable(lapis-arc-fixture "${SOSC_ROOT}/tests/arc_protocol_fixture.c"
        "${SOSC_ROOT}/src/serialosc-device/lease.c"
        "${SOSC_ROOT}/src/serialosc-device/lease_runtime.c"
        "${SOSC_ROOT}/src/serialosc-device/surface.c"
        "${SOSC_ROOT}/src/serialosc-device/config.c"
        "${SOSC_ROOT}/src/serialosc-device/config_persistence.c"
        "${SOSC_ROOT}/src/common/network.c"
        "${SOSC_ROOT}/src/serialosc-device/osc/sys_methods.c"
        "${SOSC_ROOT}/src/serialosc-device/osc/lease_methods.c")
    target_compile_definitions(lapis-arc-fixture PRIVATE _GNU_SOURCE)
    target_link_libraries(lapis-arc-fixture PRIVATE lapis_test_common liblo_static confuse)
    target_compile_options(lapis-arc-fixture PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
    set_property(TEST lapis-python-tests APPEND PROPERTY ENVIRONMENT "LAPIS_ARC_FIXTURE=$<TARGET_FILE:lapis-arc-fixture>")
endif()

if(SOSC_LAPIS_STRICT)
    add_subdirectory("${SOSC_ROOT}/third-party/libuv" libuv EXCLUDE_FROM_ALL)
    add_executable(lapis-supervisor-fixture "${SOSC_ROOT}/tests/supervisor_fixture.c"
        "${SOSC_ROOT}/src/common/network.c" "${SOSC_ROOT}/src/serialosc-device/surface.c"
        "${SOSC_ROOT}/src/common/lapis_usb_policy.c")
    target_include_directories(lapis-supervisor-fixture PRIVATE "${SOSC_ROOT}/third-party")
    target_compile_definitions(lapis-supervisor-fixture PRIVATE VERSION="fixture" GIT_COMMIT="fixture")
    target_compile_options(lapis-supervisor-fixture PRIVATE -Wno-incompatible-pointer-types)
    target_link_libraries(lapis-supervisor-fixture PRIVATE lapis_test_common liblo_static uv_a)
    set_target_properties(lapis-supervisor-fixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin")
    foreach(child serialosc-device serialosc-detector)
        configure_file("${SOSC_ROOT}/platforms/lapis/supervisor-tests/fake_child.py"
            "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin/${child}" COPYONLY)
        file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin/${child}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    endforeach()
    add_test(NAME lapis-supervisor-shutdown COMMAND "${Python3_EXECUTABLE}"
        "${SOSC_ROOT}/platforms/lapis/supervisor-tests/test_shutdown.py"
        --fixture "$<TARGET_FILE:lapis-supervisor-fixture>")
    set_tests_properties(lapis-supervisor-shutdown PROPERTIES TIMEOUT 30 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

add_mocked_lease_test(lapis-lease-deadline lease_deadline_test.c
    "${SOSC_ROOT}/src/serialosc-device/event_delivery.c"
    "${SOSC_ROOT}/src/serialosc-device/osc/sys_methods.c"
    "${SOSC_ROOT}/src/serialosc-device/osc/util.c")
target_link_options(lapis-lease-deadline PRIVATE -Wl,--wrap=sosc_monotonic_ms)

add_executable(lapis-strict-osc-surface "${SOSC_ROOT}/tests/strict_osc_surface_test.c"
    "${SOSC_ROOT}/src/serialosc-device/osc/mext_methods.c"
    "${SOSC_ROOT}/src/serialosc-device/osc/util.c")
target_link_libraries(lapis-strict-osc-surface PRIVATE lapis_test_common liblo_static)
target_compile_options(lapis-strict-osc-surface PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
add_test(NAME lapis-strict-osc-surface COMMAND lapis-strict-osc-surface)
set_tests_properties(lapis-strict-osc-surface PROPERTIES TIMEOUT 15)
