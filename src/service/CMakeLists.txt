include(WindowsBuildHelpers)

set(SERVICE_SOURCES main.cpp)
if(WIN32)
    list(APPEND SERVICE_SOURCES WinServiceAdapter.cpp)
endif()

add_executable(hub32api-service ${SERVICE_SOURCES})

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

if(WIN32)
    add_windows_resources(hub32api-service
        DESCRIPTION "hub32api Windows Service"
        ORIGINAL_FILENAME "hub32api-service.exe"
    )
endif()

install(TARGETS hub32api-service
    RUNTIME DESTINATION bin
)
