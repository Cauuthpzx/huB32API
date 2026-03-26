# hub32agent -- lightweight agent binary
# Connects to hub32api server, executes commands on student PCs.

option(HUB32_WITH_FFMPEG "Build agent with FFmpeg H.264 encoding (NVENC/QSV/x264)" OFF)

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
    src/encode/EncoderFactory.cpp
    src/encode/CpuColorConverter.cpp
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

set_project_warnings(hub32agent-service)

# -----------------------------------------------------------------------
# FFmpeg encoding support (optional — for H.264 streaming)
# -----------------------------------------------------------------------
if(HUB32_WITH_FFMPEG)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET
            libavcodec libavutil libswscale)
        if(FFMPEG_FOUND)
            target_link_libraries(hub32agent-service PRIVATE PkgConfig::FFMPEG)
        endif()
    endif()
    target_compile_definitions(hub32agent-service PRIVATE HUB32_WITH_FFMPEG)

    target_sources(hub32agent-service PRIVATE
        src/encode/X264Encoder.cpp
    )

    # TODO: Add remaining encoder implementation sources when ready:
    # target_sources(hub32agent-service PRIVATE
    #     src/encode/NvencEncoder.cpp
    #     src/encode/QsvEncoder.cpp
    #     src/encode/D3D11ColorConverter.cpp
    # )
endif()

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

    if(HUB32_WITH_FFMPEG)
        add_executable(test_x264_encoder
            tests/test_x264_encoder.cpp
            src/encode/X264Encoder.cpp
            src/encode/CpuColorConverter.cpp
            src/encode/EncoderFactory.cpp
        )
        target_include_directories(test_x264_encoder PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/encode
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include
        )
        target_link_libraries(test_x264_encoder PRIVATE
            nlohmann_json::nlohmann_json
            spdlog::spdlog
            GTest::gtest_main
        )
        target_compile_definitions(test_x264_encoder PRIVATE HUB32_WITH_FFMPEG)

        # Link FFmpeg
        if(PkgConfig_FOUND AND FFMPEG_FOUND)
            target_link_libraries(test_x264_encoder PRIVATE PkgConfig::FFMPEG)
        endif()

        add_test(NAME test_x264_encoder COMMAND test_x264_encoder)
    endif()
endif()
