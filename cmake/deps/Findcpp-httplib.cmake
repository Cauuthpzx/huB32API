# Findcpp-httplib.cmake
find_package(httplib CONFIG QUIET)
if(NOT httplib_FOUND)
    FetchContent_Declare(cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.16.0
    )
    FetchContent_MakeAvailable(cpp-httplib)
endif()
