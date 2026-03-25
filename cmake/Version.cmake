# Version.cmake
# Integrates SemVer with git tag metadata.

find_package(Git QUIET)

function(get_git_version OUT_VERSION)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_TAG
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(${OUT_VERSION} "${GIT_TAG}" PARENT_SCOPE)
    else()
        set(${OUT_VERSION} "${PROJECT_VERSION}" PARENT_SCOPE)
    endif()
endfunction()
