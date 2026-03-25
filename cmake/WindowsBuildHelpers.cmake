# WindowsBuildHelpers.cmake
# Generates .rc and .manifest files for Windows executables and DLLs.
# Mirrors Hub32's cmake/modules/WindowsBuildHelpers.cmake pattern.

if(NOT WIN32)
    return()
endif()

set(HUB32API_RC_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/templates/resource.rc.in")
set(HUB32API_MANIFEST_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/templates/app.manifest.in")

function(add_windows_resources target)
    cmake_parse_arguments(ARG "" "DESCRIPTION;ORIGINAL_FILENAME" "" ${ARGN})

    set(RC_DESCRIPTION     "${ARG_DESCRIPTION}")
    set(RC_FILENAME        "${ARG_ORIGINAL_FILENAME}")
    set(RC_VERSION_MAJOR   "${PROJECT_VERSION_MAJOR}")
    set(RC_VERSION_MINOR   "${PROJECT_VERSION_MINOR}")
    set(RC_VERSION_PATCH   "${PROJECT_VERSION_PATCH}")
    set(RC_VERSION_STRING  "${PROJECT_VERSION}")

    set(RC_OUT "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
    configure_file("${HUB32API_RC_TEMPLATE}" "${RC_OUT}" @ONLY)
    target_sources(${target} PRIVATE "${RC_OUT}")

    set(MANIFEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/${target}.manifest")
    configure_file("${HUB32API_MANIFEST_TEMPLATE}" "${MANIFEST_OUT}" @ONLY)
    set_target_properties(${target} PROPERTIES
        VS_USER_PROPS "${MANIFEST_OUT}"
    )
endfunction()
