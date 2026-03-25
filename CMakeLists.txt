cmake_minimum_required(VERSION 3.20)

project(hub32api
    VERSION 1.0.0
    DESCRIPTION "REST API server for Hub32 educational monitoring system"
    HOMEPAGE_URL "https://github.com/hub32/hub32api"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------
# C++ standard
# -----------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------
# CMake modules
# -----------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps")

include(ProjectDefaults)
include(CompilerWarnings)
include(Version)
include(WindowsBuildHelpers)
include(PchHelpers)
include(BuildPlugin)

# -----------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------
option(BUILD_TESTS        "Build test suite"                    OFF)
option(BUILD_EXAMPLES     "Build C++ example programs"          OFF)
option(BUILD_DOCS         "Build Doxygen documentation"         OFF)
option(BUILD_PLUGINS      "Build optional plugin targets"       ON)
option(WITH_PCH           "Use precompiled headers"             ON)
option(WITH_SANITIZERS    "Enable address/UB sanitizers"        OFF)
option(HUB32API_SHARED  "Build as shared library (DLL)"       ON)

# -----------------------------------------------------------------------
# Dependencies (FetchContent or Find)
# -----------------------------------------------------------------------
include(FindOrFetchDeps)

# -----------------------------------------------------------------------
# Generate version header
# -----------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/hub32api/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/hub32api/version.h"
    @ONLY
)

# -----------------------------------------------------------------------
# Generate export header
# -----------------------------------------------------------------------
include(GenerateExportHeader)

# -----------------------------------------------------------------------
# Enable testing early so agent/ and other subdirectories can add_test()
# -----------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
endif()

# -----------------------------------------------------------------------
# Main targets
# -----------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(agent)

# -----------------------------------------------------------------------
# Optional components
# -----------------------------------------------------------------------
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_DOCS)
    add_subdirectory(docs)
endif()

# -----------------------------------------------------------------------
# Install rules
# -----------------------------------------------------------------------
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/hub32api"
    DESTINATION include
)
install(
    DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/hub32api"
    DESTINATION include
)
