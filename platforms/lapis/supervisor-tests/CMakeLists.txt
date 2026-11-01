cmake_minimum_required(VERSION 3.27)
project(serialosc_lapis_supervisor_fixture LANGUAGES C)
set(CMAKE_C_STANDARD 17)
get_filename_component(SOSC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${SOSC_ROOT}/platforms/lapis/cmake/PrivateDependencies.cmake")
lapis_patched_dependency(liblo "${SOSC_ROOT}/third-party/liblo"
    "${SOSC_ROOT}/platforms/lapis/patches/liblo-explicit-unicast-bind.patch" SOSC_LO)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(WITH_STATIC ON)
set(WITH_TESTS OFF)
set(WITH_CPP_TESTS OFF)
set(WITH_TOOLS OFF)
set(WITH_EXAMPLES OFF)
add_subdirectory("${SOSC_LO}/cmake" liblo EXCLUDE_FROM_ALL)
add_subdirectory("${SOSC_ROOT}/third-party/libuv" libuv EXCLUDE_FROM_ALL)
add_executable(lapis-supervisor-fixture
    "${SOSC_ROOT}/tests/supervisor_fixture.c"
    "${SOSC_ROOT}/src/common/ipc.c"
    "${SOSC_ROOT}/src/common/util.c"
    "${SOSC_ROOT}/src/common/platform/posix.c"
    "${SOSC_ROOT}/src/common/network.c"
    "${SOSC_ROOT}/src/common/lapis_usb_policy.c"
    "${SOSC_ROOT}/src/serialosc-device/surface.c")
target_include_directories(lapis-supervisor-fixture PRIVATE
    "${SOSC_ROOT}/include" "${SOSC_ROOT}/third-party"
    "${SOSC_ROOT}/third-party/libmonome/public"
    "${SOSC_LO}" "${CMAKE_CURRENT_BINARY_DIR}/liblo")
target_compile_definitions(lapis-supervisor-fixture PRIVATE
    _GNU_SOURCE SOSC_LAPIS_STRICT SOSC_LAPIS_LOOPBACK_ONLY
    VERSION="fixture" GIT_COMMIT="software-only")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lapis-supervisor-fixture PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-incompatible-pointer-types
        -Wno-missing-field-initializers -Wno-sign-compare)
endif()
target_link_libraries(lapis-supervisor-fixture PRIVATE liblo_static uv_a)
set_target_properties(lapis-supervisor-fixture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin")
# Only inert helpers are copied beside the fixture. No detector, serial I/O,
# libudev, libmonome implementation, or production worker target is built.
configure_file(fake_child.py "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin/serialosc-detector" COPYONLY)
configure_file(fake_child.py "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin/serialosc-device" COPYONLY)
file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin/serialosc-detector"
    "${CMAKE_CURRENT_BINARY_DIR}/fixture-bin/serialosc-device"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_test(NAME lapis-supervisor-shutdown COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/test_shutdown.py"
    --fixture "$<TARGET_FILE:lapis-supervisor-fixture>")
set_tests_properties(lapis-supervisor-shutdown PROPERTIES TIMEOUT 30
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
