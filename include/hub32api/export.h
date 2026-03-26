#pragma once

// DLL export/import macros for hub32api-core shared library.
// Generated via CMake's generate_export_header(); do not edit manually.

#ifdef HUB32API_STATIC
#  define HUB32API_EXPORT
#  define HUB32API_NO_EXPORT
#elif defined(_WIN32)
#  ifdef hub32api_core_EXPORTS
#    define HUB32API_EXPORT __declspec(dllexport)
#  else
#    define HUB32API_EXPORT __declspec(dllimport)
#  endif
#  define HUB32API_NO_EXPORT
#  define HUB32API_DEPRECATED        __declspec(deprecated)
#  define HUB32API_DEPRECATED_EXPORT HUB32API_EXPORT HUB32API_DEPRECATED
#else
// Linux/macOS: use GCC visibility attributes
#  if defined(__GNUC__) || defined(__clang__)
#    ifdef hub32api_core_EXPORTS
#      define HUB32API_EXPORT __attribute__((visibility("default")))
#    else
#      define HUB32API_EXPORT __attribute__((visibility("default")))
#    endif
#    define HUB32API_NO_EXPORT __attribute__((visibility("hidden")))
#  else
#    define HUB32API_EXPORT
#    define HUB32API_NO_EXPORT
#  endif
#  define HUB32API_DEPRECATED        __attribute__((deprecated))
#  define HUB32API_DEPRECATED_EXPORT HUB32API_EXPORT HUB32API_DEPRECATED
#endif
