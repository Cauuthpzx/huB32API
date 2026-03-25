include(WindowsBuildHelpers)

add_executable(veyon32api-service
    main.cpp
    WinServiceAdapter.cpp
)

target_include_directories(veyon32api-service
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(veyon32api-service
    PRIVATE
        veyon32api-core
        veyon32api-server
)

add_windows_resources(veyon32api-service
    DESCRIPTION "veyon32api Windows Service"
    ORIGINAL_FILENAME "veyon32api-service.exe"
)

install(TARGETS veyon32api-service
    RUNTIME DESTINATION bin
)
