# FindLibDataChannel.cmake — FetchContent for libdatachannel
include(FetchContent)

FetchContent_Declare(libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.24.1
    GIT_SHALLOW    TRUE
)

# Disable features we don't need
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)

# Disable -Werror in libsrtp and libdatachannel (format warnings on MinGW/GCC)
set(BUILD_WITH_WARNINGS OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

# Allow older CMake version in bundled deps (plog, usrsctp, etc.)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

FetchContent_MakeAvailable(libdatachannel)

# Fix: global NOMINMAX (from ProjectDefaults.cmake) removes Windows min/max
# macros, but usrsctp C code relies on them. Pass -D directly to compiler
# since CMake drops function-style definitions from target_compile_definitions.
if(TARGET usrsctp)
    target_compile_options(usrsctp PRIVATE
        "-Dmin(a,b)=((a)<(b)?(a):(b))"
        "-Dmax(a,b)=((a)>(b)?(a):(b))"
    )
endif()

# Fix: libsrtp has -Werror that fails on MinGW format warnings.
# Strip -Werror from srtp2 compile options.
if(TARGET srtp2)
    get_target_property(_srtp2_opts srtp2 COMPILE_OPTIONS)
    if(_srtp2_opts)
        list(REMOVE_ITEM _srtp2_opts "-Werror" "/WX")
        set_target_properties(srtp2 PROPERTIES COMPILE_OPTIONS "${_srtp2_opts}")
    endif()
endif()
