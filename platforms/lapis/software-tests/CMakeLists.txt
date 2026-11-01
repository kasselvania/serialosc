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
add_subdirectory("${SOSC_ROOT}/third-party/liblo/cmake" liblo EXCLUDE_FROM_ALL)
find_package(Threads REQUIRED)
add_library(lapis_test_common OBJECT
    "${SOSC_ROOT}/src/common/ipc.c"
    "${SOSC_ROOT}/src/common/util.c"
    "${SOSC_ROOT}/src/common/platform/posix.c")
target_include_directories(lapis_test_common PUBLIC
    "${SOSC_ROOT}/include" "${SOSC_ROOT}/third-party/libmonome/public"
    "${SOSC_ROOT}/third-party/liblo" "${CMAKE_CURRENT_BINARY_DIR}/liblo")
target_compile_definitions(lapis_test_common PRIVATE _GNU_SOURCE)

function(add_mocked_lease_test name file)
    add_executable(${name}
        "${SOSC_ROOT}/tests/${file}"
        "${SOSC_ROOT}/src/serialosc-device/lease.c"
        "${SOSC_ROOT}/src/serialosc-device/lease_runtime.c"
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
