include(WindowsBuildHelpers)

add_executable(hub32api-service
    main.cpp
    WinServiceAdapter.cpp
)

target_include_directories(hub32api-service
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(hub32api-service
    PRIVATE
        hub32api-core
        hub32api-server
)

add_windows_resources(hub32api-service
    DESCRIPTION "hub32api Windows Service"
    ORIGINAL_FILENAME "hub32api-service.exe"
)

install(TARGETS hub32api-service
    RUNTIME DESTINATION bin
)
