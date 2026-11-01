# Lapis-only dependency copies: never modify tracked git submodules in place.
function(lapis_patched_dependency name source patchfile output_var)
    # Pin touched input content as well as patch context: no fuzzy drift.
    if(name STREQUAL "libmonome")
        set(inputs src/proto/mext.c src/platform/posix.c src/platform/linux.c)
        set(expected
            6c25f8668db1920858cc9b794bba105e5a631bc1b5f130ca4a3239b0cc7391b5
            a9af49a634b9b3db3e3e5afa3024d8bf2afb026131f658c50e3b328365a7d891
            6d8ac43722d3f656539ead18fb687fc87ca0c9802330bb41aa7225e709ec3149)
    elseif(name STREQUAL "liblo")
        set(inputs src/server.c)
        set(expected c7ba509c9584978d2ed6b8793a0230696572938e4a346d1b0cdfb2ee7aeb81bf)
    else()
        message(FATAL_ERROR "Unknown Lapis private dependency: ${name}")
    endif()
    list(LENGTH inputs count)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        list(GET inputs ${index} input)
        list(GET expected ${index} wanted)
        file(SHA256 "${source}/${input}" actual)
        if(NOT actual STREQUAL wanted)
            message(FATAL_ERROR "Unexpected ${name} baseline: ${input}")
        endif()
    endforeach()
    set(dest "${CMAKE_BINARY_DIR}/lapis-private/${name}")
    # Re-copy clean source on configure; patches fail closed if upstream changes.
    file(REMOVE_RECURSE "${dest}")
    file(MAKE_DIRECTORY "${dest}")
    file(COPY "${source}/" DESTINATION "${dest}" PATTERN ".git" EXCLUDE)
    find_program(LAPIS_PATCH_EXECUTABLE patch REQUIRED)
    execute_process(COMMAND "${LAPIS_PATCH_EXECUTABLE}" --batch --forward --fuzz=0 -p1 -i "${patchfile}"
        WORKING_DIRECTORY "${dest}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Lapis private ${name} patch failed: ${out} ${err}")
    endif()
    set(${output_var} "${dest}" PARENT_SCOPE)
endfunction()
