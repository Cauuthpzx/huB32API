# FindJwt-cpp.cmake
find_package(jwt-cpp CONFIG QUIET)
if(NOT jwt-cpp_FOUND)
    FetchContent_Declare(jwt-cpp
        GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
        GIT_TAG        v0.7.0
    )
    FetchContent_MakeAvailable(jwt-cpp)
endif()
