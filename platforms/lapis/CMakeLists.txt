cmake_minimum_required(VERSION 3.27)
project(serialosc_lapis_offline_checks VERSION 0.1.0 LANGUAGES C)

# Deliberately do not add the repository's top-level CMake project: it builds
# hardware-accessing executables. This project builds the pure lease core only.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(SOSC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
enable_testing()
add_executable(lapis-lease-core-test
    "${SOSC_SOURCE_ROOT}/tests/lease_test.c"
    "${SOSC_SOURCE_ROOT}/src/serialosc-device/lease.c")
target_include_directories(lapis-lease-core-test PRIVATE "${SOSC_SOURCE_ROOT}/include")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lapis-lease-core-test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME lapis-lease-core COMMAND lapis-lease-core-test)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME lapis-passive-doctor
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_LIST_DIR}/tests" -v)
set_tests_properties(lapis-passive-doctor PROPERTIES
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
