# Refresh at build time, so a commit or dirty checkout is not mistaken for the
# revision from an old configure. Only changed content recompiles main.cpp.
execute_process(COMMAND git -C "${SOURCE_ROOT}" describe --always --dirty --abbrev=12 "--exclude=*"
    OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT revision MATCHES "^[0-9a-f]+(-dirty)?$")
    set(revision "unavailable")
endif()
set(contents "// Generated at build time. Do not edit.\n#define LO_BUILD_REVISION \"${revision}\"\n")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
    if(previous STREQUAL contents)
        return()
    endif()
endif()
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${OUTPUT}" "${contents}")
