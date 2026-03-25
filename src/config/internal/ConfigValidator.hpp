#pragma once

#include <string>
#include <vector>
#include "hub32api/config/ServerConfig.hpp"

namespace hub32api::config::internal {

// -----------------------------------------------------------------------
// ConfigValidator — validates ServerConfig on load.
// Returns list of error messages; empty = valid.
// -----------------------------------------------------------------------
class ConfigValidator
{
public:
    std::vector<std::string> validate(const ServerConfig& cfg) const;
};

} // namespace hub32api::config::internal
