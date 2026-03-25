# FindSQLite3.cmake
# Temporarily remove our custom module path to avoid infinite recursion
# when find_package(SQLite3) resolves back to this file.
set(_hub32_saved_module_path "${CMAKE_MODULE_PATH}")
list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps")
find_package(SQLite3 QUIET)
set(CMAKE_MODULE_PATH "${_hub32_saved_module_path}")
unset(_hub32_saved_module_path)
if(NOT SQLite3_FOUND)
    FetchContent_Declare(sqlite3
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip
    )
    FetchContent_MakeAvailable(sqlite3)
    if(NOT TARGET SQLite::SQLite3)
        add_library(SQLite3 STATIC
            ${sqlite3_SOURCE_DIR}/sqlite3.c
        )
        target_include_directories(SQLite3 PUBLIC ${sqlite3_SOURCE_DIR})
        add_library(SQLite::SQLite3 ALIAS SQLite3)
    endif()
endif()
