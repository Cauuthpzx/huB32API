#include "PrecompiledHeader.hpp"
#include "internal/Hub32CoreWrapper.hpp"

// TODO: Include Hub32Core.h once hub32-core is linked
// #include <Hub32Core.h>

namespace hub32api::core::internal {

struct Hub32CoreWrapper::Impl
{
    // std::unique_ptr<QCoreApplication> qtApp;
    // std::unique_ptr<Hub32Core> core;
    bool initialized = false;
    std::string pluginDir;
};

Hub32CoreWrapper::Hub32CoreWrapper(const ServerConfig& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->pluginDir = cfg.hub32PluginDir;
    // TODO: Initialize Qt application context
    // TODO: Initialize Hub32Core with Component::CLI
    m_impl->initialized = false; // placeholder
    spdlog::info("[Hub32CoreWrapper] stub initialized");
}

Hub32CoreWrapper::~Hub32CoreWrapper() = default;

bool Hub32CoreWrapper::isInitialized() const noexcept
{
    return m_impl->initialized;
}

std::string Hub32CoreWrapper::hub32Version() const
{
    return "4.10.x"; // TODO: return Hub32Core::versionString()
}

std::string Hub32CoreWrapper::pluginDirectory() const
{
    return m_impl->pluginDir;
}

Hub32Core* Hub32CoreWrapper::core() noexcept
{
    return nullptr; // TODO: return m_impl->core.get()
}

} // namespace hub32api::core::internal
