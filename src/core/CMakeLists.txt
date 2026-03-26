include(PchHelpers)
include(GenerateExportHeader)

add_library(hub32api-core SHARED
    Hub32CoreWrapper.cpp
    PluginRegistry.cpp
    ConnectionPool.cpp
    ApiContext.cpp
    AuditLog.cpp
    I18n.cpp
    CryptoUtils.cpp
    Constants.cpp
    ../utils/string_utils.cpp
    ../auth/JwtAuth.cpp
    ../auth/internal/JwtValidator.cpp
    ../auth/internal/TokenStore.cpp
    ../auth/Hub32KeyAuth.cpp
    ../auth/UserRoleStore.cpp
    ../config/ServerConfig.cpp
    ../config/internal/ConfigValidator.cpp
    ../agent/AgentRegistry.cpp
    ../agent/HeartbeatMonitor.cpp
)

generate_export_header(hub32api-core
    BASE_NAME HUB32API
    EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/include/hub32api/export_generated.h"
)

target_include_directories(hub32api-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hub32api-core
    PUBLIC
        nlohmann_json::nlohmann_json
        spdlog::spdlog
    PRIVATE
        jwt-cpp::jwt-cpp
        SQLite::SQLite3
        ws2_32
        crypt32
        $<IF:$<TARGET_EXISTS:OpenSSL::SSL>,OpenSSL::SSL,ssl>
        $<IF:$<TARGET_EXISTS:OpenSSL::Crypto>,OpenSSL::Crypto,crypto>
)

target_include_directories(hub32api-core
    SYSTEM PRIVATE
        ${CMAKE_BINARY_DIR}/_deps/jwt-cpp-src/include
)

set_project_warnings(hub32api-core)
add_pch(hub32api-core "${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeader.hpp")

install(TARGETS hub32api-core
    LIBRARY  DESTINATION lib
    RUNTIME  DESTINATION bin
    ARCHIVE  DESTINATION lib
)
