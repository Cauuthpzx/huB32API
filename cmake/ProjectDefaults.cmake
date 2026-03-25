# ProjectDefaults.cmake
# Sets project-wide CMake policies and defaults.

cmake_policy(SET CMP0077 NEW)   # option() honors normal variables
cmake_policy(SET CMP0091 NEW)   # MSVC runtime library via CMAKE_MSVC_RUNTIME_LIBRARY
cmake_policy(SET CMP0092 NEW)   # MSVC: no /W3 injected by default

# Windows: Unicode build + target Windows 10
if(WIN32)
    # _WIN32_WINNT=0x0A00 → Windows 10 (required for CreateFile2 in cpp-httplib)
    add_compile_definitions(
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 WINVER=0x0A00
    )
    # Only add -mwindows for GUI subsystem executables (services use console)
    # add_link_options(-mwindows)  # disabled: service needs console subsystem
endif()

# Position-independent code for shared libs
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Install prefix default
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "C:/Program Files/veyon32api" CACHE PATH "" FORCE)
endif()
