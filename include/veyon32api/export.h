#pragma once

// DLL export/import macros for veyon32api-core shared library.
// Generated via CMake's generate_export_header(); do not edit manually.

#ifdef VEYON32API_STATIC
#  define VEYON32API_EXPORT
#  define VEYON32API_NO_EXPORT
#else
#  ifdef veyon32api_core_EXPORTS
#    define VEYON32API_EXPORT __declspec(dllexport)
#  else
#    define VEYON32API_EXPORT __declspec(dllimport)
#  endif
#  define VEYON32API_NO_EXPORT
#endif

#define VEYON32API_DEPRECATED        __declspec(deprecated)
#define VEYON32API_DEPRECATED_EXPORT VEYON32API_EXPORT VEYON32API_DEPRECATED
