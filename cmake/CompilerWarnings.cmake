# CompilerWarnings.cmake
# Applies strict warning flags per compiler.

function(set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /wd4100 /wd4505
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wno-unused-parameter
            -Wno-missing-field-initializers
        )
    endif()
endfunction()
