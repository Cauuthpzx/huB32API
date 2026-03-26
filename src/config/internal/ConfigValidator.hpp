#pragma once

#include <string>
#include <vector>
#include "hub32api/config/ServerConfig.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api::config::internal {

// -----------------------------------------------------------------------
// ConfigValidator — validates ServerConfig on load.
// Returns Result<vector<string>>: ok with warnings list, or fail on critical errors.
// -----------------------------------------------------------------------
class ConfigValidator
{
public:
    Result<std::vector<std::string>> validate(const ServerConfig& cfg) const;
};

} // namespace hub32api::config::internal
