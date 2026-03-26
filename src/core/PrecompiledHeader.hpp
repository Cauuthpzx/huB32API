#pragma once

// -----------------------------------------------------------------------
// Precompiled header for hub32api.
// Includes stable, frequently-used headers to speed up compilation.
// -----------------------------------------------------------------------

// Standard library
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <variant>
#include <memory>
#include <functional>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <mutex>
#include <thread>
#include <atomic>

// Windows
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winsvc.h>
#endif

// Third-party (stable)
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

// Project
#include "hub32api/export.h"
#include "hub32api/core/Types.hpp"
#include "hub32api/core/Error.hpp"
#include "hub32api/core/Result.hpp"
#include "hub32api/core/Constants.hpp"
