include(PchHelpers)
include(GenerateExportHeader)

add_library(veyon32api-core SHARED
    VeyonCoreWrapper.cpp
    PluginRegistry.cpp
    ConnectionPool.cpp
    ApiContext.cpp
    ../auth/JwtAuth.cpp
    ../auth/internal/JwtValidator.cpp
    ../auth/internal/TokenStore.cpp
    ../auth/VeyonKeyAuth.cpp
    ../config/ServerConfig.cpp
)

generate_export_header(veyon32api-core
    BASE_NAME VEYON32API
    EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/include/veyon32api/export_generated.h"
)

target_include_directories(veyon32api-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(veyon32api-core
    PUBLIC
        nlohmann_json::nlohmann_json
        spdlog::spdlog
    PRIVATE
        ws2_32
        crypt32
)

add_pch(veyon32api-core "${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeader.hpp")

install(TARGETS veyon32api-core
    LIBRARY  DESTINATION lib
    RUNTIME  DESTINATION bin
    ARCHIVE  DESTINATION lib
)
