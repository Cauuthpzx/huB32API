include(PchHelpers)
include(WindowsBuildHelpers)

add_library(veyon32api-server SHARED
    HttpServer.cpp
    Router.cpp
    ThreadPool.cpp

    # v1 controllers
    ../api/v1/controllers/AuthController.cpp
    ../api/v1/controllers/ComputerController.cpp
    ../api/v1/controllers/FeatureController.cpp
    ../api/v1/controllers/FramebufferController.cpp
    ../api/v1/controllers/SessionController.cpp

    # v1 middleware
    ../api/v1/middleware/AuthMiddleware.cpp
    ../api/v1/middleware/CorsMiddleware.cpp
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

target_include_directories(veyon32api-server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(veyon32api-server
    PUBLIC
        veyon32api-core
    PRIVATE
        httplib::httplib
        ws2_32
)

add_pch(veyon32api-server "${CMAKE_SOURCE_DIR}/src/core/PrecompiledHeader.hpp")

add_windows_resources(veyon32api-server
    DESCRIPTION "veyon32api HTTP Server"
    ORIGINAL_FILENAME "veyon32api-server.dll"
)

install(TARGETS veyon32api-server
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)
