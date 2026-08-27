cmake_minimum_required(VERSION 3.27)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

project(serialosc VERSION 1.4.8)

set(CMAKE_C_STANDARD 17)

include(CTest)

message(STATUS "configuring libuv")

add_subdirectory(third-party/libuv EXCLUDE_FROM_ALL)

message(STATUS "configuring confuse")

add_subdirectory(third-party/confuse EXCLUDE_FROM_ALL)

message(STATUS "configuring libmonome")

add_subdirectory(third-party/libmonome EXCLUDE_FROM_ALL)

message(STATUS "configuring liblo")

# ensure WITH_STATIC is not cleared
# when loading third-party/liblo/cmake for compatibility reasons
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(WITH_STATIC ON)
set(WITH_TESTS OFF)
set(WITH_CPP_TESTS OFF)
set(WITH_TOOLS OFF)
set(WITH_EXAMPLES OFF)
add_subdirectory(third-party/liblo/cmake EXCLUDE_FROM_ALL)

message(STATUS "configuring serialosc")

include_directories(include)
include_directories(third-party/liblo)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/third-party/liblo/cmake)

message(STATUS "checking git commit id")
execute_process(COMMAND "git" "rev-parse" "--verify" "--short" "HEAD" RESULT_VARIABLE GIT_COMMIT_RESULT OUTPUT_VARIABLE GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT GIT_COMMIT_RESULT EQUAL 0)
    set(GIT_COMMIT "unknown")
endif()

message(STATUS "checking git commit id - ${GIT_COMMIT}")

if(WIN32)
    set(RC_VERSION ${CMAKE_PROJECT_VERSION_MAJOR},${CMAKE_PROJECT_VERSION_MINOR},${CMAKE_PROJECT_VERSION_PATCH})
    set(RC_VERSION_STRING "${PROJECT_VERSION} (${GIT_COMMIT})")

    set(RC_EXECUTABLE "serialosc-detector${CMAKE_EXECUTABLE_SUFFIX}")
    configure_file(src/common/platform/winres/serialosc-cmake.rc.in
        ${CMAKE_BINARY_DIR}/winres/serialosc-detector.rc)

    set(RC_EXECUTABLE "serialosc-device${CMAKE_EXECUTABLE_SUFFIX}")
    configure_file(src/common/platform/winres/serialosc-cmake.rc.in
        ${CMAKE_BINARY_DIR}/winres/serialosc-device.rc)

    set(RC_EXECUTABLE "serialoscd${CMAKE_EXECUTABLE_SUFFIX}")
    configure_file(src/common/platform/winres/serialosc-cmake.rc.in
        ${CMAKE_BINARY_DIR}/winres/serialoscd.rc)

    file(COPY_FILE src/common/platform/winres/logo.ico ${CMAKE_BINARY_DIR}/winres/logo.ico)
endif()

# common

add_library(serialosc_common OBJECT
    src/common/ipc.c
    src/common/util.c)

if(LINUX)
    target_sources(serialosc_common PRIVATE src/common/platform/posix.c)
    target_sources(serialosc_common PRIVATE src/common/platform/linux.c)
endif()

if(APPLE)
    target_sources(serialosc_common PRIVATE src/common/platform/posix.c)
    target_sources(serialosc_common PRIVATE src/common/platform/darwin.c)
endif()

if(WIN32)
    target_sources(serialosc_common PRIVATE src/common/platform/windows.c)
endif()

target_link_libraries(serialosc_common monome_static)

# serialosc-detector

add_executable(serialosc-detector)
set_target_properties(serialosc-detector PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(LINUX)
    include(FindPkgConfig)
    pkg_check_modules(libudev REQUIRED IMPORTED_TARGET libudev)

    target_sources(serialosc-detector PRIVATE src/serialosc-detector/libudev.c)
    target_link_libraries(serialosc-detector PkgConfig::libudev)
endif()

if(APPLE)
    target_sources(serialosc-detector PRIVATE src/serialosc-detector/iokitlib.c)
    target_link_libraries(serialosc-detector "-framework IOKit")
    target_link_libraries(serialosc-detector "-framework CoreFoundation")
endif()

if(WIN32)
    target_sources(serialosc-detector PRIVATE src/serialosc-detector/windows.c)
    target_sources(serialosc-detector PRIVATE ${CMAKE_BINARY_DIR}/winres/serialosc-detector.rc)
endif()

target_link_libraries(serialosc-detector serialosc_common)

# serialosc-device

if(NOT WIN32)
    message(STATUS "checking for working poll()")

    try_run(HAVE_WORKING_POLL_RUN_RESULT HAVE_WORKING_POLL_COMPILE_RESULT SOURCE_FROM_CONTENT working_poll.c [=[
        #include <fcntl.h>
        #include <stdlib.h>
        #include <poll.h>

        int main(int argc, char **argv) {
            struct pollfd fds[1];

            fds[0].fd = open("/dev/null", 1);
            fds[0].events = POLLIN;

            if (poll(fds, 1, 0) < 0 || fds[0].revents & POLLNVAL)
                exit(1);
            exit(0);
        }
    ]=])

    if(HAVE_WORKING_POLL_COMPILE_RESULT AND HAVE_WORKING_POLL_RUN_RESULT EQUAL 0)
        message(STATUS "checking for working poll() - found")
        set(HAVE_WORKING_POLL ON)
    else()
        message(STATUS "checking for working poll() - not found, will use select()")
        set(HAVE_WORKING_POLL OFF)
    endif()
endif()

if(WIN32)
    set(HAVE_DNS_SD ON)
else()
    check_include_file("dns_sd.h" HAVE_DNS_SD)
endif()

cmake_dependent_option(build_with_zeroconf "enable zeroconf support" ON HAVE_DNS_SD OFF)

add_executable(serialosc-device)
set_target_properties(serialosc-device PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(serialosc-device PRIVATE src/serialosc-device/config.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/lease.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/lease_runtime.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/server.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/main.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/osc/util.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/osc/sys_methods.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/osc/lease_methods.c)
target_sources(serialosc-device PRIVATE src/serialosc-device/osc/mext_methods.c)

if(WIN32)
    target_sources(serialosc-device PRIVATE src/serialosc-device/event_loop/windows.c)
    target_sources(serialosc-device PRIVATE ${CMAKE_BINARY_DIR}/winres/serialosc-device.rc)
else()
    if(HAVE_WORKING_POLL)
        target_sources(serialosc-device PRIVATE src/serialosc-device/event_loop/poll.c)
    else()
        target_sources(serialosc-device PRIVATE src/serialosc-device/event_loop/select.c)
    endif()
endif()

if(build_with_zeroconf)
    add_compile_definitions(SOSC_ZEROCONF)

    if(WIN32)
        target_sources(serialosc-device PRIVATE src/serialosc-device/zeroconf/windows.c)
        target_link_libraries(serialosc-device dnsapi)
    else()
        target_sources(serialosc-device PRIVATE src/serialosc-device/zeroconf/common.c)
        if(LINUX)
            target_sources(serialosc-device PRIVATE src/serialosc-device/zeroconf/not_darwin.c)
        endif()
        if(APPLE)
            target_sources(serialosc-device PRIVATE src/serialosc-device/zeroconf/darwin.c)
        endif()
    endif()
else()
    target_sources(serialosc-device PRIVATE src/serialosc-device/zeroconf/dummy.c)
endif()

target_include_directories(serialosc-device PRIVATE ${CMAKE_SOURCE_DIR}/third-party)
target_link_libraries(serialosc-device serialosc_common confuse monome_static liblo_static uv_a)

# serialoscd

add_executable(serialoscd)
set_target_properties(serialoscd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(WIN32)
    target_sources(serialoscd PRIVATE src/serialoscd/win_svc.c)
    target_sources(serialoscd PRIVATE ${CMAKE_BINARY_DIR}/winres/serialoscd.rc)
endif()

target_sources(serialoscd PRIVATE src/serialoscd/uv.c)

# TODO: fix the actual warnings
if(NOT MSVC)
    target_compile_options(serialoscd PRIVATE -Wno-incompatible-pointer-types)
endif()

target_include_directories(serialoscd PRIVATE ${CMAKE_SOURCE_DIR}/third-party)
target_compile_definitions(serialoscd PRIVATE VERSION="${PROJECT_VERSION}")
target_compile_definitions(serialoscd PRIVATE GIT_COMMIT="${GIT_COMMIT}")
target_link_libraries(serialoscd serialosc_common liblo_static uv_a)

if(BUILD_TESTING)
    add_executable(serialosc-lease-test
        tests/lease_test.c
        src/serialosc-device/lease.c)
    target_include_directories(serialosc-lease-test PRIVATE include)
    add_test(NAME serialosc-lease-test COMMAND serialosc-lease-test)

    add_executable(serialosc-lease-runtime-test
        tests/lease_runtime_test.c
        src/serialosc-device/lease.c
        src/serialosc-device/lease_runtime.c)
    target_include_directories(serialosc-lease-runtime-test PRIVATE include)
    target_link_libraries(serialosc-lease-runtime-test
        serialosc_common liblo_static)
    if(NOT MSVC)
        target_compile_options(serialosc-lease-runtime-test PRIVATE
            -Wall -Wextra -Werror)
    endif()
    add_test(NAME serialosc-lease-runtime-test
        COMMAND serialosc-lease-runtime-test)

    add_executable(serialosc-lease-osc-test
        tests/lease_osc_test.c
        src/serialosc-device/lease.c
        src/serialosc-device/lease_runtime.c
        src/serialosc-device/osc/lease_methods.c
        src/serialosc-device/osc/sys_methods.c)
    target_include_directories(serialosc-lease-osc-test PRIVATE include)
    target_link_libraries(serialosc-lease-osc-test
        serialosc_common liblo_static)
    if(NOT MSVC)
        target_compile_options(serialosc-lease-osc-test PRIVATE
            -Wall -Wextra -Werror -Wno-unused-parameter)
    endif()
    add_test(NAME serialosc-lease-osc-test COMMAND serialosc-lease-osc-test)

    if(NOT WIN32)
        find_package(Threads REQUIRED)
        add_executable(serialosc-lease-event-loop-test
            tests/lease_event_loop_test.c
            src/serialosc-device/lease.c
            src/serialosc-device/lease_runtime.c)
        if(HAVE_WORKING_POLL)
            target_sources(serialosc-lease-event-loop-test PRIVATE
                src/serialosc-device/event_loop/poll.c)
        else()
            target_sources(serialosc-lease-event-loop-test PRIVATE
                src/serialosc-device/event_loop/select.c)
        endif()
        target_include_directories(serialosc-lease-event-loop-test PRIVATE
            include)
        target_link_libraries(serialosc-lease-event-loop-test
            serialosc_common liblo_static Threads::Threads)
        if(NOT MSVC)
            target_compile_options(serialosc-lease-event-loop-test PRIVATE
                -Wall -Wextra -Werror)
        endif()
        add_test(NAME serialosc-lease-event-loop-test
            COMMAND serialosc-lease-event-loop-test)
    endif()
endif()

message(STATUS "configuration summary:

    version:    ${PROJECT_VERSION} (${GIT_COMMIT})
    prefix:     ${CMAKE_INSTALL_PREFIX}
    zeroconf:   ${build_with_zeroconf}
")
