#include "PrecompiledHeader.hpp"
#include "internal/VeyonCoreWrapper.hpp"

// TODO: Include VeyonCore.h once veyon-core is linked
// #include <VeyonCore.h>

namespace veyon32api::core::internal {

struct VeyonCoreWrapper::Impl
{
    // std::unique_ptr<QCoreApplication> qtApp;
    // std::unique_ptr<VeyonCore> core;
    bool initialized = false;
    std::string pluginDir;
};

VeyonCoreWrapper::VeyonCoreWrapper(const ServerConfig& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->pluginDir = cfg.veyonPluginDir;
    // TODO: Initialize Qt application context
    // TODO: Initialize VeyonCore with Component::CLI
    m_impl->initialized = false; // placeholder
    spdlog::info("[VeyonCoreWrapper] stub initialized");
}

VeyonCoreWrapper::~VeyonCoreWrapper() = default;

bool VeyonCoreWrapper::isInitialized() const noexcept
{
    return m_impl->initialized;
}

std::string VeyonCoreWrapper::veyonVersion() const
{
    return "4.10.x"; // TODO: return VeyonCore::versionString()
}

std::string VeyonCoreWrapper::pluginDirectory() const
{
    return m_impl->pluginDir;
}

VeyonCore* VeyonCoreWrapper::core() noexcept
{
    return nullptr; // TODO: return m_impl->core.get()
}

} // namespace veyon32api::core::internal
