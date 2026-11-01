# Require both a failing exit and our diagnostic, not a crash or launch error.
execute_process(COMMAND "${TEST_PROGRAM}" --fail
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT "${result}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Expected a nonzero exit from the check: ${result}")
endif()
string(FIND "${error}" "check failed: ++evaluations == 0" diagnostic)
if(diagnostic EQUAL -1)
    message(FATAL_ERROR "Missing failed-check diagnostic: ${error}")
endif()
message(STATUS "Always-on negative check verified (exit ${result})")
