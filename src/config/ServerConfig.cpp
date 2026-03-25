#include "../core/PrecompiledHeader.hpp"
#include "hub32api/config/ServerConfig.hpp"
#include "internal/ConfigValidator.hpp"

namespace hub32api {

ServerConfig ServerConfig::defaults()
{
    return ServerConfig{};
}

ServerConfig ServerConfig::from_file(const std::string& path)
{
    // TODO: open path, parse JSON with nlohmann::json, populate ServerConfig
    spdlog::info("[ServerConfig] loading from file: {}", path);
    return defaults();
}

ServerConfig ServerConfig::from_registry()
{
    // TODO: read from Windows Registry under HKLM\SOFTWARE\hub32api
    spdlog::info("[ServerConfig] loading from Windows Registry");
    return defaults();
}

} // namespace hub32api
