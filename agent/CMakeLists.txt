# hub32agent -- lightweight agent binary
# Connects to hub32api server, executes commands on student PCs.

add_executable(hub32agent-service
    src/main.cpp
    src/AgentConfig.cpp
    src/AgentClient.cpp
    src/CommandDispatcher.cpp
    src/WinServiceAdapter.cpp
    src/features/ScreenCapture.cpp
    src/features/ScreenLock.cpp
    src/features/InputLock.cpp
    src/features/MessageDisplay.cpp
    src/features/PowerControl.cpp
)

target_include_directories(hub32agent-service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(hub32agent-service
    PRIVATE
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        httplib::httplib
        ws2_32
        d3d11
        dxgi
        dxguid
        ole32
)

# Windows resources (optional version info)
if(WIN32)
    target_compile_definitions(hub32agent-service PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

install(TARGETS hub32agent-service RUNTIME DESTINATION bin)

# -----------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------
if(BUILD_TESTS)
    add_executable(test_command_dispatcher
        tests/test_command_dispatcher.cpp
        src/CommandDispatcher.cpp
    )
    target_include_directories(test_command_dispatcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
    )
    target_link_libraries(test_command_dispatcher PRIVATE
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        GTest::gtest_main
    )
    add_test(NAME test_command_dispatcher COMMAND test_command_dispatcher)
endif()
