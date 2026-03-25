# PchHelpers.cmake
# Adds precompiled header support (mirrors Veyon's PchHelpers.cmake).

function(add_pch target pch_header)
    if(WITH_PCH)
        target_precompile_headers(${target} PRIVATE "${pch_header}")
    endif()
endfunction()
