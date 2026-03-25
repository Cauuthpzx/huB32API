# FindJwt-cpp.cmake
# jwt-cpp requires OpenSSL for HMAC/RSA algorithms.
find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    # On Windows with MinGW/MSYS2, OpenSSL is typically available as a system package.
    # If not found, linking will use the bare ssl/crypto names from the core CMakeLists.
    message(STATUS "OpenSSL not found via find_package; jwt-cpp will rely on system ssl/crypto")
endif()

find_package(jwt-cpp CONFIG QUIET)
if(NOT jwt-cpp_FOUND)
    FetchContent_Declare(jwt-cpp
        GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
        GIT_TAG        v0.7.0
    )
    FetchContent_MakeAvailable(jwt-cpp)
endif()
