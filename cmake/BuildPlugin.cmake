# BuildPlugin.cmake
# build_veyon32_plugin() macro — mirrors Veyon's build_veyon_plugin() pattern.

include(PchHelpers)
include(WindowsBuildHelpers)
include(GenerateExportHeader)

macro(build_veyon32_plugin)
    cmake_parse_arguments(PLUGIN
        ""
        "NAME;DESCRIPTION"
        "SOURCES;HEADERS;LINK_LIBRARIES"
        ${ARGN}
    )

    set(_target "veyon32api-${PLUGIN_NAME}")

    add_library(${_target} SHARED
        ${PLUGIN_SOURCES}
        ${PLUGIN_HEADERS}
    )

    generate_export_header(${_target}
        BASE_NAME "${_target}"
        EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/export_${PLUGIN_NAME}.h"
    )

    target_include_directories(${_target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(${_target}
        PRIVATE
            veyon32api-core
            ${PLUGIN_LINK_LIBRARIES}
    )

    add_windows_resources(${_target}
        DESCRIPTION "${PLUGIN_DESCRIPTION}"
        ORIGINAL_FILENAME "${_target}.dll"
    )

    add_pch(${_target} "${CMAKE_SOURCE_DIR}/src/core/PrecompiledHeader.hpp")

    install(TARGETS ${_target}
        LIBRARY DESTINATION lib/veyon32api/plugins
        RUNTIME DESTINATION bin/veyon32api/plugins
    )
endmacro()
