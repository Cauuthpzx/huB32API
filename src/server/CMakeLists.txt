include(PchHelpers)
include(WindowsBuildHelpers)

add_library(hub32api-server SHARED
    HttpServer.cpp
    Router.cpp
    SseManager.cpp
    ThreadPool.cpp

    # common utilities
    ../api/common/HttpErrorUtil.cpp

    # v1 controllers
    ../api/v1/controllers/AgentController.cpp
    ../api/v1/controllers/AuthController.cpp
    ../api/v1/controllers/ComputerController.cpp
    ../api/v1/controllers/FeatureController.cpp
    ../api/v1/controllers/FramebufferController.cpp
    ../api/v1/controllers/SessionController.cpp
    ../api/v1/controllers/SchoolController.cpp
    ../api/v1/controllers/StreamController.cpp
    ../api/v1/controllers/TeacherController.cpp

    # v1 middleware
    ../api/v1/middleware/AuthMiddleware.cpp
    ../api/v1/middleware/CorsMiddleware.cpp
    ../api/v1/middleware/InputValidationMiddleware.cpp
    ../api/v1/middleware/LoggingMiddleware.cpp
    ../api/v1/middleware/RateLimitMiddleware.cpp

    # v2 controllers
    ../api/v2/controllers/BatchController.cpp
    ../api/v2/controllers/LocationController.cpp
    ../api/v2/controllers/MetricsController.cpp

    # plugins (compiled into server for now; can be split to shared DLLs later)
    ../plugins/computer/ComputerPlugin.cpp
    ../plugins/computer/NetworkDirectoryBridge.cpp
    ../plugins/feature/FeaturePlugin.cpp
    ../plugins/feature/FeatureRegistry.cpp
    ../plugins/session/SessionPlugin.cpp
    ../plugins/metrics/MetricsPlugin.cpp
)

target_include_directories(hub32api-server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(hub32api-server
    PUBLIC
        hub32api-core
    PRIVATE
        httplib::httplib
        $<$<PLATFORM_ID:Windows>:ws2_32>
        $<$<PLATFORM_ID:Windows>:iphlpapi>
)

set_project_warnings(hub32api-server)
add_pch(hub32api-server "${CMAKE_SOURCE_DIR}/src/core/PrecompiledHeader.hpp")

if(WIN32)
    add_windows_resources(hub32api-server
        DESCRIPTION "hub32api HTTP Server"
        ORIGINAL_FILENAME "hub32api-server.dll"
    )
endif()

install(TARGETS hub32api-server
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)
