# FindOrFetchDeps.cmake
# Aggregator that includes individual dependency finders.

include(FetchContent)

include(deps/Findcpp-httplib)
include(deps/FindNlohmannJson)
include(deps/Findspdlog)
include(deps/FindJwt-cpp)
include(deps/FindSQLite3)
include(deps/FindGoogleTest)
include(deps/FindArgon2)

# Mark all FetchContent dependency include dirs as SYSTEM so that
# -Werror doesn't fire on third-party code (jwt-cpp, spdlog, etc.)
# CMake 3.25+ supports SYSTEM on FetchContent_Declare; for older versions
# we use set_target_properties on the imported targets.
function(mark_as_system target)
    if(TARGET ${target})
        get_target_property(_aliased ${target} ALIASED_TARGET)
        if(_aliased)
            set(_real ${_aliased})
        else()
            set(_real ${target})
        endif()
        get_target_property(_inc ${_real} INTERFACE_INCLUDE_DIRECTORIES)
        if(_inc)
            set_target_properties(${_real} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
        endif()
    endif()
endfunction()

mark_as_system(httplib::httplib)
mark_as_system(nlohmann_json::nlohmann_json)
mark_as_system(spdlog::spdlog)
mark_as_system(jwt-cpp::jwt-cpp)
mark_as_system(SQLite::SQLite3)
mark_as_system(GTest::gtest)
mark_as_system(GTest::gmock)
mark_as_system(Argon2::argon2)
