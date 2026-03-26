# CompilerWarnings.cmake
# Applies strict warning flags per compiler.
# Note: Third-party headers must be included as SYSTEM to avoid triggering
#       these warnings in external code (jwt-cpp, spdlog, etc.)

function(set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /wd4100 /wd4505
            /w14265   # class has virtual functions, but destructor is not virtual
            /w14640   # thread-unsafe construction of local statics
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wshadow
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual
            -Wnull-dereference
            -Wformat=2
            -Wno-unused-parameter
            -Wno-missing-field-initializers
            # Temporarily disabled until codebase is clean:
            # -Wold-style-cast     (too many hits in Windows API macros)
            # -Wconversion         (too many hits in third-party headers)
            # -Wsign-conversion    (too many hits in third-party headers)
        )
    endif()
endfunction()
