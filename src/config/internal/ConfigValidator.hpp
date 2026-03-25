#pragma once

#include <string>
#include <vector>
#include "veyon32api/config/ServerConfig.hpp"

namespace veyon32api::config::internal {

// -----------------------------------------------------------------------
// ConfigValidator — validates ServerConfig on load.
// Returns list of error messages; empty = valid.
// -----------------------------------------------------------------------
class ConfigValidator
{
public:
    std::vector<std::string> validate(const ServerConfig& cfg) const;
};

} // namespace veyon32api::config::internal
