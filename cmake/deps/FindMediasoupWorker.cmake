# FindMediasoupWorker.cmake
# Finds or builds libmediasoup-worker for the mediasoup SFU backend.
#
# This module is only used when HUB32_WITH_MEDIASOUP=ON (Linux only).
# On Windows, MockSfuBackend is used instead.
#
# Provides:
#   mediasoup::worker   - imported static library target
#
# Required system dependencies (must be installed):
#   libuv, libsrtp2, openssl, abseil-cpp
#
# Usage in CMakeLists.txt:
#   find_package(MediasoupWorker REQUIRED)
#   target_link_libraries(myapp PRIVATE mediasoup::worker)

if(WIN32)
    message(WARNING "MediasoupWorker: mediasoup worker is Linux-only. "
                    "Use MockSfuBackend on Windows.")
    return()
endif()

set(MEDIASOUP_WORKER_DIR "${CMAKE_SOURCE_DIR}/third_party/mediasoup/worker")
set(MEDIASOUP_SERVER_REF_DIR "${CMAKE_SOURCE_DIR}/third_party/mediasoup-server-ref")

# --- Option 1: Pre-built library ---
find_library(MEDIASOUP_WORKER_LIB
    NAMES mediasoup-worker
    PATHS
        "${MEDIASOUP_WORKER_DIR}/build"
        "${MEDIASOUP_SERVER_REF_DIR}/build"
    PATH_SUFFIXES lib Release Debug
)

if(MEDIASOUP_WORKER_LIB)
    message(STATUS "Found pre-built mediasoup-worker: ${MEDIASOUP_WORKER_LIB}")
else()
    # --- Option 2: Build via ExternalProject ---
    include(ExternalProject)
    message(STATUS "MediasoupWorker: will build from source via meson")

    ExternalProject_Add(mediasoup_worker_build
        SOURCE_DIR "${MEDIASOUP_WORKER_DIR}"
        CONFIGURE_COMMAND meson setup
            --buildtype=release
            --default-library=static
            -Dmediasoup-worker-fuzzer=false
            <BINARY_DIR>
            <SOURCE_DIR>
        BUILD_COMMAND ninja -C <BINARY_DIR>
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "<BINARY_DIR>/libmediasoup-worker.a"
    )

    set(MEDIASOUP_WORKER_LIB "<BINARY_DIR>/libmediasoup-worker.a")
endif()

# --- Find required system dependencies ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBUV QUIET libuv)
    pkg_check_modules(LIBSRTP2 QUIET libsrtp2)
endif()

# Fallback: find libuv manually
if(NOT LIBUV_FOUND)
    find_library(LIBUV_LIBRARIES NAMES uv uv_a)
    find_path(LIBUV_INCLUDE_DIRS NAMES uv.h)
    if(LIBUV_LIBRARIES AND LIBUV_INCLUDE_DIRS)
        set(LIBUV_FOUND TRUE)
    endif()
endif()

# --- Create imported target ---
if(NOT TARGET mediasoup::worker)
    add_library(mediasoup::worker STATIC IMPORTED GLOBAL)
    set_target_properties(mediasoup::worker PROPERTIES
        IMPORTED_LOCATION "${MEDIASOUP_WORKER_LIB}"
    )
    target_include_directories(mediasoup::worker INTERFACE
        "${MEDIASOUP_WORKER_DIR}/include"
        "${MEDIASOUP_WORKER_DIR}/fbs"
    )
    if(LIBUV_FOUND)
        target_link_libraries(mediasoup::worker INTERFACE ${LIBUV_LIBRARIES})
    endif()
    if(LIBSRTP2_FOUND)
        target_link_libraries(mediasoup::worker INTERFACE ${LIBSRTP2_LIBRARIES})
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MediasoupWorker
    REQUIRED_VARS MEDIASOUP_WORKER_LIB
    FAIL_MESSAGE "mediasoup-worker not found. Build it first: cd third_party/mediasoup/worker && meson setup build && ninja -C build"
)
