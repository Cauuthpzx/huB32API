# FindArgon2.cmake — FetchContent for phc-winner-argon2 (reference implementation)
include(FetchContent)

FetchContent_Declare(argon2
    GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2.git
    GIT_TAG        20190702
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(argon2)

if(NOT TARGET Argon2::argon2)
    add_library(argon2_lib STATIC
        ${argon2_SOURCE_DIR}/src/argon2.c
        ${argon2_SOURCE_DIR}/src/core.c
        ${argon2_SOURCE_DIR}/src/encoding.c
        ${argon2_SOURCE_DIR}/src/thread.c
        ${argon2_SOURCE_DIR}/src/blake2/blake2b.c
        ${argon2_SOURCE_DIR}/src/ref.c
    )
    target_include_directories(argon2_lib PUBLIC
        ${argon2_SOURCE_DIR}/include
    )
    target_compile_definitions(argon2_lib PRIVATE ARGON2_NO_THREADS)
    if(MSVC)
        target_compile_options(argon2_lib PRIVATE /w)
    else()
        target_compile_options(argon2_lib PRIVATE -w)
    endif()
    add_library(Argon2::argon2 ALIAS argon2_lib)
endif()
